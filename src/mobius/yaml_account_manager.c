/*
 * yaml_account_manager.c - YAML-based account manager
 *
 * Maps to: internal/mobius/account_manager.go
 *
 * Each account is stored as {login}.yaml in the accounts directory.
 * Supports the boolean-flag Access format used by Mobius >= v0.17.0.
 */

#include "mobius/yaml_account_manager.h"
#include "hotline/access.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_ACCOUNTS 256

/* The access flag name → bit mapping now lives in src/hotline/access.c.
 * This file uses hl_access_bit_name() and hl_access_name_to_bit() for
 * lookups in both directions. */

/* Concrete YAML account manager */
typedef struct {
    hl_account_mgr_t  base;
    hl_account_t      accounts[MAX_ACCOUNTS];
    int               account_count;
    char              account_dir[1024];
    pthread_mutex_t   mu;
} yaml_account_mgr_t;

/* Parse a single account YAML file */
static int parse_account_file(hl_account_t *acct, const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    yaml_parser_t parser;
    yaml_event_t event;

    if (!yaml_parser_initialize(&parser)) {
        fclose(f);
        return -1;
    }
    yaml_parser_set_input_file(&parser, f);

    memset(acct, 0, sizeof(*acct));

    char current_key[128] = {0};
    int in_access_map = 0;
    int depth = 0;
    int done = 0;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) break;

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            depth++;
            if (depth == 2 && strcmp(current_key, "Access") == 0) {
                in_access_map = 1;
                current_key[0] = '\0'; /* Reset key for access map parsing */
            }
            break;

        case YAML_MAPPING_END_EVENT:
            if (in_access_map && depth == 2) {
                in_access_map = 0;
            }
            depth--;
            break;

        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;

            if (in_access_map) {
                if (current_key[0] == '\0') {
                    strncpy(current_key, val, sizeof(current_key) - 1);
                } else {
                    /* Set access bit if value is "true" */
                    if (strcmp(val, "true") == 0) {
                        int bit = hl_access_name_to_bit(current_key);
                        if (bit >= 0) hl_access_set(acct->access, bit);
                    }
                    current_key[0] = '\0';
                }
            } else if (depth == 1) {
                if (current_key[0] == '\0') {
                    strncpy(current_key, val, sizeof(current_key) - 1);
                } else {
                    if (strcmp(current_key, "Login") == 0)
                        strncpy(acct->login, val, sizeof(acct->login) - 1);
                    else if (strcmp(current_key, "Name") == 0)
                        strncpy(acct->name, val, sizeof(acct->name) - 1);
                    else if (strcmp(current_key, "Password") == 0)
                        strncpy(acct->password, val, sizeof(acct->password) - 1);
                    else if (strcmp(current_key, "FileRoot") == 0)
                        strncpy(acct->file_root, val, sizeof(acct->file_root) - 1);
                    else if (strcmp(current_key, "RequireEncryption") == 0)
                        acct->require_encryption = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
                    else if (strcmp(current_key, "Color") == 0) {
                        /* Parse "#RRGGBB" (case-insensitive). Invalid -> 0 (absent). */
                        const char *p = val;
                        if (*p == '#') p++;
                        if (strlen(p) == 6) {
                            uint32_t result = 0;
                            int i, ok = 1;
                            for (i = 0; i < 6; i++) {
                                char c = p[i];
                                int nibble;
                                if (c >= '0' && c <= '9') nibble = c - '0';
                                else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
                                else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
                                else { ok = 0; break; }
                                result = (result << 4) | (uint32_t)nibble;
                            }
                            if (ok) acct->nick_color = result & 0x00FFFFFFu;
                            else {
                                fprintf(stderr, "yaml_account_manager: invalid hex in Color '%s', treating as absent\n", val);
                            }
                        } else {
                            fprintf(stderr, "yaml_account_manager: invalid Color '%s' (expected #RRGGBB), treating as absent\n", val);
                        }
                    }

                    current_key[0] = '\0';
                }
            }
            break;
        }

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

/* Write an account to a YAML file */
static int write_account_yaml(const char *dir, const hl_account_t *acct)
{
    char filepath[2048];
    snprintf(filepath, sizeof(filepath), "%s/%s.yaml", dir, acct->login);

    FILE *f = fopen(filepath, "w");
    if (!f) return -1;

    fprintf(f, "Login: %s\n", acct->login);
    fprintf(f, "Name: %s\n", acct->name);
    fprintf(f, "Password: \"%s\"\n", acct->password);
    fprintf(f, "Access:\n");

    /* Iterate every possible bit (0..63) and emit any name-mapped bit
     * that's currently set. Uses the shared access.c map. */
    int bit;
    for (bit = 0; bit < 64; bit++) {
        if (hl_access_is_set(acct->access, bit)) {
            const char *name = hl_access_bit_name(bit);
            if (name) fprintf(f, "  %s: true\n", name);
        }
    }

    if (acct->file_root[0] != '\0') {
        fprintf(f, "FileRoot: %s\n", acct->file_root);
    }

    if (acct->nick_color != 0) {
        fprintf(f, "Color: \"#%06X\"\n", acct->nick_color & 0x00FFFFFFu);
    }

    if (acct->require_encryption) {
        fprintf(f, "RequireEncryption: true\n");
    }

    fclose(f);
    return 0;
}

static int delete_account_yaml(const char *dir, const char *login)
{
    char filepath[2048];
    snprintf(filepath, sizeof(filepath), "%s/%s.yaml", dir, login);
    return unlink(filepath);
}

/* vtable methods */

static hl_account_t *yaml_get(hl_account_mgr_t *self, const char *login)
{
    yaml_account_mgr_t *mgr = (yaml_account_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    int i;
    for (i = 0; i < mgr->account_count; i++) {
        if (strcmp(mgr->accounts[i].login, login) == 0) {
            pthread_mutex_unlock(&mgr->mu);
            return &mgr->accounts[i];
        }
    }

    pthread_mutex_unlock(&mgr->mu);
    return NULL;
}

static hl_account_t **yaml_list(hl_account_mgr_t *self, int *out_count)
{
    yaml_account_mgr_t *mgr = (yaml_account_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    if (mgr->account_count == 0) {
        pthread_mutex_unlock(&mgr->mu);
        *out_count = 0;
        return NULL;
    }

    hl_account_t **arr = (hl_account_t **)malloc(
        (size_t)mgr->account_count * sizeof(hl_account_t *));
    if (!arr) {
        pthread_mutex_unlock(&mgr->mu);
        *out_count = 0;
        return NULL;
    }

    int i;
    for (i = 0; i < mgr->account_count; i++) {
        arr[i] = &mgr->accounts[i];
    }
    *out_count = mgr->account_count;

    pthread_mutex_unlock(&mgr->mu);
    return arr;
}

static int yaml_create(hl_account_mgr_t *self, const hl_account_t *acct)
{
    yaml_account_mgr_t *mgr = (yaml_account_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    if (mgr->account_count >= MAX_ACCOUNTS) {
        pthread_mutex_unlock(&mgr->mu);
        return -1;
    }

    memcpy(&mgr->accounts[mgr->account_count], acct, sizeof(hl_account_t));
    mgr->account_count++;

    write_account_yaml(mgr->account_dir, acct);

    pthread_mutex_unlock(&mgr->mu);
    return 0;
}

static int yaml_update(hl_account_mgr_t *self, const hl_account_t *acct,
                       const char *new_login)
{
    yaml_account_mgr_t *mgr = (yaml_account_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    int i;
    for (i = 0; i < mgr->account_count; i++) {
        if (strcmp(mgr->accounts[i].login, acct->login) == 0) {
            memcpy(&mgr->accounts[i], acct, sizeof(hl_account_t));
            if (new_login && new_login[0] != '\0') {
                strncpy(mgr->accounts[i].login, new_login,
                        sizeof(mgr->accounts[i].login) - 1);
            }
            write_account_yaml(mgr->account_dir, &mgr->accounts[i]);
            pthread_mutex_unlock(&mgr->mu);
            return 0;
        }
    }

    pthread_mutex_unlock(&mgr->mu);
    return -1;
}

static int yaml_del(hl_account_mgr_t *self, const char *login)
{
    yaml_account_mgr_t *mgr = (yaml_account_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    int i;
    for (i = 0; i < mgr->account_count; i++) {
        if (strcmp(mgr->accounts[i].login, login) == 0) {
            /* Shift remaining accounts down */
            int j;
            for (j = i; j < mgr->account_count - 1; j++) {
                memcpy(&mgr->accounts[j], &mgr->accounts[j + 1], sizeof(hl_account_t));
            }
            mgr->account_count--;

            delete_account_yaml(mgr->account_dir, login);
            pthread_mutex_unlock(&mgr->mu);
            return 0;
        }
    }

    pthread_mutex_unlock(&mgr->mu);
    return -1;
}

static const hl_account_mgr_vtable_t yaml_vtable = {
    .get    = yaml_get,
    .list   = yaml_list,
    .create = yaml_create,
    .update = yaml_update,
    .del    = yaml_del
};

hl_account_mgr_t *mobius_yaml_account_mgr_new(const char *account_dir)
{
    yaml_account_mgr_t *mgr = (yaml_account_mgr_t *)calloc(1, sizeof(yaml_account_mgr_t));
    if (!mgr) return NULL;

    mgr->base.vt = &yaml_vtable;
    strncpy(mgr->account_dir, account_dir, sizeof(mgr->account_dir) - 1);
    pthread_mutex_init(&mgr->mu, NULL);

    /* Load all .yaml files from directory */
    DIR *dir = opendir(account_dir);
    if (!dir) {
        /* Directory doesn't exist — start with no accounts */
        return &mgr->base;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && mgr->account_count < MAX_ACCOUNTS) {
        size_t namelen = strlen(entry->d_name);
        if (namelen < 6) continue; /* need at least "x.yaml" */
        if (strcmp(entry->d_name + namelen - 5, ".yaml") != 0) continue;

        char filepath[2048];
        snprintf(filepath, sizeof(filepath), "%s/%s", account_dir, entry->d_name);

        if (parse_account_file(&mgr->accounts[mgr->account_count], filepath) == 0) {
            mgr->account_count++;
        }
    }
    closedir(dir);

    return &mgr->base;
}

void mobius_yaml_account_mgr_free(hl_account_mgr_t *base)
{
    if (!base) return;
    yaml_account_mgr_t *mgr = (yaml_account_mgr_t *)base;
    pthread_mutex_destroy(&mgr->mu);
    free(mgr);
}
