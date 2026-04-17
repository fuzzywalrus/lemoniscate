/*
 * ban_file.c - YAML-based ban list manager
 *
 * Maps to: internal/mobius/ban.go (BanFile)
 *
 * Stores IP bans, username bans, and nickname bans in a single YAML file.
 */

#include "mobius/ban_file.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_BANS 512

typedef struct {
    char ip[64];
    int  permanent; /* 1 = permanent, 0 = timed (not yet implemented) */
} ip_ban_t;

typedef struct {
    char name[128];
} name_ban_t;

/* Concrete ban file manager */
typedef struct {
    hl_ban_mgr_t    base;
    ip_ban_t        ip_bans[MAX_BANS];
    int             ip_ban_count;
    name_ban_t      user_bans[MAX_BANS];
    int             user_ban_count;
    name_ban_t      nick_bans[MAX_BANS];
    int             nick_ban_count;
    char            file_path[1024];
    pthread_mutex_t mu;
} ban_file_t;

/* Load bans from YAML */
static int load_bans(ban_file_t *bf)
{
    FILE *f = fopen(bf->file_path, "rb");
    if (!f) return 0; /* No ban file = no bans, OK */

    yaml_parser_t parser;
    yaml_event_t event;

    if (!yaml_parser_initialize(&parser)) {
        fclose(f);
        return -1;
    }
    yaml_parser_set_input_file(&parser, f);

    /* Simple state machine for the ban file format */
    char current_key[128] = {0};
    int in_ban_list = 0, in_banned_users = 0, in_banned_nicks = 0;
    int done = 0;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) break;

        switch (event.type) {
        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;

            if (current_key[0] == '\0') {
                /* Check for section keys */
                if (strcmp(val, "banList") == 0) {
                    in_ban_list = 1; in_banned_users = 0; in_banned_nicks = 0;
                } else if (strcmp(val, "bannedUsers") == 0) {
                    in_ban_list = 0; in_banned_users = 1; in_banned_nicks = 0;
                } else if (strcmp(val, "bannedNicks") == 0) {
                    in_ban_list = 0; in_banned_users = 0; in_banned_nicks = 1;
                } else {
                    strncpy(current_key, val, sizeof(current_key) - 1);
                }
            } else {
                if (in_ban_list && bf->ip_ban_count < MAX_BANS) {
                    strncpy(bf->ip_bans[bf->ip_ban_count].ip, current_key,
                            sizeof(bf->ip_bans[0].ip) - 1);
                    bf->ip_bans[bf->ip_ban_count].permanent =
                        (strcmp(val, "null") == 0 || strcmp(val, "~") == 0);
                    bf->ip_ban_count++;
                } else if (in_banned_users && bf->user_ban_count < MAX_BANS) {
                    strncpy(bf->user_bans[bf->user_ban_count].name, current_key,
                            sizeof(bf->user_bans[0].name) - 1);
                    bf->user_ban_count++;
                } else if (in_banned_nicks && bf->nick_ban_count < MAX_BANS) {
                    strncpy(bf->nick_bans[bf->nick_ban_count].name, current_key,
                            sizeof(bf->nick_bans[0].name) - 1);
                    bf->nick_ban_count++;
                }
                current_key[0] = '\0';
            }
            break;
        }

        case YAML_MAPPING_START_EVENT:
        case YAML_MAPPING_END_EVENT:
            break;

        case YAML_STREAM_END_EVENT:
            done = 1;
            break;

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&parser);
    fclose(f);
    return 0;
}

/* vtable methods */

static int ban_is_banned(hl_ban_mgr_t *self, const char *ip)
{
    ban_file_t *bf = (ban_file_t *)self;
    pthread_mutex_lock(&bf->mu);
    int i;
    for (i = 0; i < bf->ip_ban_count; i++) {
        if (strcmp(bf->ip_bans[i].ip, ip) == 0) {
            pthread_mutex_unlock(&bf->mu);
            return 1;
        }
    }
    pthread_mutex_unlock(&bf->mu);
    return 0;
}

static int ban_is_username_banned(hl_ban_mgr_t *self, const char *username)
{
    ban_file_t *bf = (ban_file_t *)self;
    pthread_mutex_lock(&bf->mu);
    int i;
    for (i = 0; i < bf->user_ban_count; i++) {
        if (strcmp(bf->user_bans[i].name, username) == 0) {
            pthread_mutex_unlock(&bf->mu);
            return 1;
        }
    }
    pthread_mutex_unlock(&bf->mu);
    return 0;
}

static int ban_is_nickname_banned(hl_ban_mgr_t *self, const char *nickname)
{
    ban_file_t *bf = (ban_file_t *)self;
    pthread_mutex_lock(&bf->mu);
    int i;
    for (i = 0; i < bf->nick_ban_count; i++) {
        if (strcmp(bf->nick_bans[i].name, nickname) == 0) {
            pthread_mutex_unlock(&bf->mu);
            return 1;
        }
    }
    pthread_mutex_unlock(&bf->mu);
    return 0;
}

static int ban_add(hl_ban_mgr_t *self, const char *ip)
{
    ban_file_t *bf = (ban_file_t *)self;
    pthread_mutex_lock(&bf->mu);
    if (bf->ip_ban_count >= MAX_BANS) {
        pthread_mutex_unlock(&bf->mu);
        return -1;
    }
    strncpy(bf->ip_bans[bf->ip_ban_count].ip, ip,
            sizeof(bf->ip_bans[0].ip) - 1);
    bf->ip_bans[bf->ip_ban_count].permanent = 1;
    bf->ip_ban_count++;
    /* Persist ban list to YAML */
    {
        FILE *f = fopen(bf->file_path, "w");
        if (f) {
            int j;
            fprintf(f, "banList:\n");
            for (j = 0; j < bf->ip_ban_count; j++) {
                fprintf(f, "  %s: ~\n", bf->ip_bans[j].ip);
            }
            fprintf(f, "bannedUsers:\n");
            for (j = 0; j < bf->user_ban_count; j++) {
                fprintf(f, "  %s: true\n", bf->user_bans[j].name);
            }
            fprintf(f, "bannedNicks:\n");
            for (j = 0; j < bf->nick_ban_count; j++) {
                fprintf(f, "  %s: true\n", bf->nick_bans[j].name);
            }
            fclose(f);
        }
    }

    pthread_mutex_unlock(&bf->mu);
    return 0;
}

static const hl_ban_mgr_vtable_t ban_vtable = {
    .is_banned          = ban_is_banned,
    .is_username_banned = ban_is_username_banned,
    .is_nickname_banned = ban_is_nickname_banned,
    .add                = ban_add
};

hl_ban_mgr_t *mobius_ban_file_new(const char *filepath)
{
    ban_file_t *bf = (ban_file_t *)calloc(1, sizeof(ban_file_t));
    if (!bf) return NULL;

    bf->base.vt = &ban_vtable;
    strncpy(bf->file_path, filepath, sizeof(bf->file_path) - 1);
    pthread_mutex_init(&bf->mu, NULL);

    load_bans(bf);

    return &bf->base;
}

void mobius_ban_file_free(hl_ban_mgr_t *base)
{
    if (!base) return;
    ban_file_t *bf = (ban_file_t *)base;
    pthread_mutex_destroy(&bf->mu);
    free(bf);
}
