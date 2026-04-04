/*
 * threaded_news_yaml.c - In-memory threaded news manager
 *
 * Maps to: internal/mobius/threaded_news.go + hotline/news.go
 *
 * Provides categories with articles. Initializes with a default
 * "General" category. Binary serialization matches the Hotline
 * NewsCategoryListData15 wire format.
 */

#include "mobius/threaded_news_yaml.h"
#include "hotline/types.h"
#include "hotline/time_conv.h"
#include <yaml.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Wire format constants */
static const uint8_t NEWS_CATEGORY[2] = {0x00, 0x03};
static const char NEWS_FLAVOR[] = "text/plain";

mobius_threaded_news_t *mobius_threaded_news_new(const char *filepath)
{
    mobius_threaded_news_t *tn = (mobius_threaded_news_t *)calloc(
        1, sizeof(mobius_threaded_news_t));
    if (!tn) return NULL;

    if (filepath)
        strncpy(tn->file_path, filepath, sizeof(tn->file_path) - 1);
    pthread_mutex_init(&tn->mu, NULL);

    /* Create default "General" category */
    tn_category_t *cat = &tn->categories[0];
    strncpy(cat->name, "General", TN_MAX_NAME_LEN - 1);
    memcpy(cat->type, NEWS_CATEGORY, 2);
    cat->next_article_id = 1;
    tn->category_count = 1;

    return tn;
}

void mobius_threaded_news_free(mobius_threaded_news_t *tn)
{
    if (!tn) return;
    pthread_mutex_destroy(&tn->mu);
    free(tn);
}

/* Escape YAML special chars in a string value.
 * \r (Hotline wire format) is written as \n (YAML) so it round-trips. */
static void yaml_write_escaped(FILE *f, const char *s, size_t len)
{
    size_t i;
    fputc('"', f);
    for (i = 0; i < len; i++) {
        if (s[i] == '"') fputs("\\\"", f);
        else if (s[i] == '\n' || s[i] == '\r') fputs("\\n", f);
        else if (s[i] == '\\') fputs("\\\\", f);
        else fputc(s[i], f);
    }
    fputc('"', f);
}

int tn_save(mobius_threaded_news_t *tn)
{
    if (!tn || tn->file_path[0] == '\0') return -1;

    pthread_mutex_lock(&tn->mu);

    FILE *f = fopen(tn->file_path, "w");
    if (!f) { pthread_mutex_unlock(&tn->mu); return -1; }

    if (tn->category_count == 0) {
        fprintf(f, "Categories: {}\n");
        fclose(f);
        pthread_mutex_unlock(&tn->mu);
        return 0;
    }

    fprintf(f, "Categories:\n");
    int i;
    for (i = 0; i < tn->category_count; i++) {
        tn_category_t *cat = &tn->categories[i];
        fprintf(f, "  ");
        yaml_write_escaped(f, cat->name, strlen(cat->name));
        fprintf(f, ":\n");
        fprintf(f, "    Name: \"%s\"\n", cat->name);
        fprintf(f, "    Type: %s\n",
                (cat->type[1] == 3) ? "category" : "bundle");

        int has_articles = 0;
        int j;
        for (j = 0; j < cat->article_count; j++) {
            if (cat->articles[j].active) { has_articles = 1; break; }
        }

        if (!has_articles) {
            fprintf(f, "    Articles: {}\n");
        } else {
            fprintf(f, "    Articles:\n");
            for (j = 0; j < cat->article_count; j++) {
                tn_article_t *art = &cat->articles[j];
                if (!art->active) continue;
                fprintf(f, "      \"%u\":\n", art->id);
                fprintf(f, "        Title: ");
                yaml_write_escaped(f, art->title, strlen(art->title));
                fprintf(f, "\n");
                fprintf(f, "        Poster: ");
                yaml_write_escaped(f, art->poster, strlen(art->poster));
                fprintf(f, "\n");
                fprintf(f, "        Date: [%u, %u, %u, %u, %u, %u, %u, %u]\n",
                        art->date[0], art->date[1], art->date[2], art->date[3],
                        art->date[4], art->date[5], art->date[6], art->date[7]);
                fprintf(f, "        ParentArt: [0, 0, 0, %u]\n", art->parent_id);
                fprintf(f, "        Body: ");
                yaml_write_escaped(f, art->data, art->data_len);
                fprintf(f, "\n");
            }
        }
    }

    fclose(f);
    pthread_mutex_unlock(&tn->mu);
    return 0;
}

/*
 * Sanitize a buffer so it contains only valid UTF-8.
 * Invalid bytes are replaced with '?' to keep libyaml happy.
 * Operates in-place and returns the (unchanged) length.
 */
static size_t sanitize_utf8(unsigned char *buf, size_t len)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = buf[i];
        int expect; /* number of continuation bytes */

        if (c < 0x80) { i++; continue; }                /* ASCII */
        else if ((c & 0xE0) == 0xC0) expect = 1;        /* 110xxxxx */
        else if ((c & 0xF0) == 0xE0) expect = 2;        /* 1110xxxx */
        else if ((c & 0xF8) == 0xF0) expect = 3;        /* 11110xxx */
        else { buf[i] = '?'; i++; continue; }           /* bad leader */

        /* Check that we have enough continuation bytes (10xxxxxx) */
        int ok = 1;
        int j;
        for (j = 1; j <= expect; j++) {
            if (i + (size_t)j >= len || (buf[i + j] & 0xC0) != 0x80) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            i += 1 + (size_t)expect;
        } else {
            buf[i] = '?';
            i++;
        }
    }
    return len;
}

/*
 * Read an entire file into a malloc'd buffer.
 * Returns NULL on error. Caller must free.
 */
static unsigned char *read_file_to_buf(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return NULL; }

    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    *out_len = nread;
    return buf;
}

/*
 * tn_load — Load threaded news from YAML using libyaml event parser.
 *
 * Reads the file into memory and sanitizes invalid UTF-8 bytes before
 * parsing (libyaml rejects the entire file on any encoding error).
 * Handles plain, quoted, and block scalar Data fields correctly.
 */
int tn_load(mobius_threaded_news_t *tn)
{
    if (!tn || tn->file_path[0] == '\0') return -1;

    size_t buf_len = 0;
    unsigned char *buf = read_file_to_buf(tn->file_path, &buf_len);
    if (!buf) return -1;

    sanitize_utf8(buf, buf_len);

    yaml_parser_t parser;
    yaml_event_t event;

    if (!yaml_parser_initialize(&parser)) { free(buf); return -1; }
    yaml_parser_set_input_string(&parser, buf, buf_len);

    pthread_mutex_lock(&tn->mu);
    tn->category_count = 0;

    int depth = 0;
    int in_categories = 0;
    int in_articles = 0;
    tn_category_t *cur_cat = NULL;
    tn_article_t *cur_art = NULL;
    int done = 0;

    char keys[8][256];
    memset(keys, 0, sizeof(keys));

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) break;

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            depth++;
            break;

        case YAML_MAPPING_END_EVENT:
            if (depth <= 3) cur_art = NULL;
            if (depth <= 2) { cur_cat = NULL; in_articles = 0; }
            if (depth <= 1) in_categories = 0;
            depth--;
            if (depth >= 0 && depth < 8) keys[depth][0] = '\0';
            break;

        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;
            size_t vlen = event.data.scalar.length;
            int d = depth < 8 ? depth : 7;

            if (keys[d][0] == '\0') {
                /* This is a key */
                strncpy(keys[d], val, sizeof(keys[d]) - 1);

                if (d == 1 && strcmp(val, "Categories") == 0)
                    in_categories = 1;
                else if (d == 2 && in_categories &&
                         strcmp(val, "SubCats") != 0) {
                    if (tn->category_count < TN_MAX_CATEGORIES) {
                        cur_cat = &tn->categories[tn->category_count];
                        memset(cur_cat, 0, sizeof(*cur_cat));
                        memcpy(cur_cat->type, "\x00\x03", 2);
                        cur_cat->next_article_id = 1;
                        strncpy(cur_cat->name, val, TN_MAX_NAME_LEN - 1);
                        tn->category_count++;
                        cur_art = NULL;
                        in_articles = 0;
                    }
                }
                else if (d == 3 && strcmp(val, "Articles") == 0)
                    in_articles = 1;
                else if (d == 4 && in_articles && cur_cat &&
                         val[0] >= '0' && val[0] <= '9') {
                    if (cur_cat->article_count < TN_MAX_ARTICLES) {
                        cur_art = &cur_cat->articles[cur_cat->article_count];
                        memset(cur_art, 0, sizeof(*cur_art));
                        cur_art->active = 1;
                        cur_art->id = (uint32_t)atoi(val);
                        if (cur_art->id >= cur_cat->next_article_id)
                            cur_cat->next_article_id = cur_art->id + 1;
                        cur_cat->article_count++;
                    }
                }
            } else {
                /* This is a value */
                const char *k = keys[d];

                if (cur_art) {
                    if (strcmp(k, "Title") == 0) {
                        if (vlen >= TN_MAX_TITLE_LEN) vlen = TN_MAX_TITLE_LEN - 1;
                        memcpy(cur_art->title, val, vlen);
                        cur_art->title[vlen] = '\0';
                    } else if (strcmp(k, "Poster") == 0) {
                        if (vlen >= TN_MAX_POSTER_LEN) vlen = TN_MAX_POSTER_LEN - 1;
                        memcpy(cur_art->poster, val, vlen);
                        cur_art->poster[vlen] = '\0';
                    } else if (strcmp(k, "Data") == 0 || strcmp(k, "Body") == 0) {
                        /* libyaml resolves block scalars and quoted strings.
                         * Convert \n to \r for Hotline wire format. */
                        size_t dpos = 0;
                        size_t si;
                        for (si = 0; si < vlen && dpos < TN_MAX_DATA_LEN - 1; si++) {
                            if (val[si] == '\n')
                                cur_art->data[dpos++] = '\r';
                            else
                                cur_art->data[dpos++] = val[si];
                        }
                        cur_art->data[dpos] = '\0';
                        cur_art->data_len = (uint16_t)dpos;
                    } else if (strcmp(k, "ParentID") == 0) {
                        cur_art->parent_id = (uint32_t)atoi(val);
                    }
                } else if (cur_cat) {
                    if (strcmp(k, "Name") == 0)
                        strncpy(cur_cat->name, val, TN_MAX_NAME_LEN - 1);
                }
                keys[d][0] = '\0';
            }
            break;
        }

        case YAML_SEQUENCE_START_EVENT: {
            int d = depth < 8 ? depth : 7;
            const char *k = keys[d];

            if (cur_art && strcmp(k, "Date") == 0) {
                int idx = 0;
                yaml_event_delete(&event);
                while (idx < 8) {
                    if (!yaml_parser_parse(&parser, &event)) break;
                    if (event.type == YAML_SCALAR_EVENT)
                        cur_art->date[idx++] = (uint8_t)atoi((const char *)event.data.scalar.value);
                    else if (event.type == YAML_SEQUENCE_END_EVENT)
                        break;
                    yaml_event_delete(&event);
                }
                if (event.type != YAML_SEQUENCE_END_EVENT) {
                    yaml_event_delete(&event);
                    while (yaml_parser_parse(&parser, &event) && event.type != YAML_SEQUENCE_END_EVENT)
                        yaml_event_delete(&event);
                }
                keys[d][0] = '\0';
            } else if (cur_art && (strcmp(k, "ParentArt") == 0 || strcmp(k, "FirstChildArtArt") == 0 || strcmp(k, "FirstChildArt") == 0)) {
                int last_val = 0;
                yaml_event_delete(&event);
                while (1) {
                    if (!yaml_parser_parse(&parser, &event)) break;
                    if (event.type == YAML_SCALAR_EVENT)
                        last_val = atoi((const char *)event.data.scalar.value);
                    else if (event.type == YAML_SEQUENCE_END_EVENT)
                        break;
                    yaml_event_delete(&event);
                }
                if (strcmp(k, "ParentArt") == 0)
                    cur_art->parent_id = (uint32_t)last_val;
                keys[d][0] = '\0';
            } else if (cur_cat && strcmp(k, "Type") == 0) {
                int type_vals[2] = {0, 3};
                int idx = 0;
                yaml_event_delete(&event);
                while (1) {
                    if (!yaml_parser_parse(&parser, &event)) break;
                    if (event.type == YAML_SCALAR_EVENT && idx < 2)
                        type_vals[idx++] = atoi((const char *)event.data.scalar.value);
                    else if (event.type == YAML_SEQUENCE_END_EVENT)
                        break;
                    yaml_event_delete(&event);
                }
                cur_cat->type[0] = (uint8_t)type_vals[0];
                cur_cat->type[1] = (uint8_t)type_vals[1];
                keys[d][0] = '\0';
            } else {
                /* Skip unknown sequences */
                keys[d][0] = '\0';
                yaml_event_delete(&event);
                while (1) {
                    if (!yaml_parser_parse(&parser, &event)) break;
                    if (event.type == YAML_SEQUENCE_END_EVENT) break;
                    yaml_event_delete(&event);
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
    free(buf);

    pthread_mutex_unlock(&tn->mu);
    return 0;
}

/* Find a category by name. Returns NULL if not found. */
static tn_category_t *find_category(mobius_threaded_news_t *tn, const char *name)
{
    int i;
    for (i = 0; i < tn->category_count; i++) {
        if (strcmp(tn->categories[i].name, name) == 0)
            return &tn->categories[i];
    }
    return NULL;
}

/* === Serialize categories for GetNewsCatNameList === */

int tn_get_categories(mobius_threaded_news_t *tn,
                      uint8_t **out_data, size_t *out_len,
                      int *out_count)
{
    pthread_mutex_lock(&tn->mu);

    /* Calculate total size needed */
    size_t total = 0;
    int i;
    for (i = 0; i < tn->category_count; i++) {
        tn_category_t *cat = &tn->categories[i];
        size_t name_len = strlen(cat->name);
        /* Type(2) + Count(2) */
        total += 4;
        /* Category type has GUID(16) + AddSN(4) + DeleteSN(4) */
        if (cat->type[0] == 0 && cat->type[1] == 3)
            total += 24;
        /* NameLen(1) + Name(n) */
        total += 1 + name_len;
    }

    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) {
        pthread_mutex_unlock(&tn->mu);
        return -1;
    }

    size_t offset = 0;
    for (i = 0; i < tn->category_count; i++) {
        tn_category_t *cat = &tn->categories[i];
        size_t name_len = strlen(cat->name);

        /* Type */
        buf[offset++] = cat->type[0];
        buf[offset++] = cat->type[1];

        /* Count (articles + subcats) */
        uint16_t count = (uint16_t)cat->article_count;
        buf[offset++] = (uint8_t)(count >> 8);
        buf[offset++] = (uint8_t)(count & 0xFF);

        /* GUID, AddSN, DeleteSN for categories */
        if (cat->type[0] == 0 && cat->type[1] == 3) {
            memcpy(buf + offset, cat->guid, 16); offset += 16;
            memcpy(buf + offset, cat->add_sn, 4); offset += 4;
            memcpy(buf + offset, cat->del_sn, 4); offset += 4;
        }

        /* NameLen + Name */
        buf[offset++] = (uint8_t)name_len;
        memcpy(buf + offset, cat->name, name_len);
        offset += name_len;
    }

    *out_data = buf;
    *out_len = offset;
    *out_count = tn->category_count;

    pthread_mutex_unlock(&tn->mu);
    return 0;
}

/* === Serialize article list for GetNewsArtNameList === */

int tn_get_article_list(mobius_threaded_news_t *tn,
                        const char *cat_name,
                        uint8_t **out_data, size_t *out_len)
{
    pthread_mutex_lock(&tn->mu);

    tn_category_t *cat = find_category(tn, cat_name);
    if (!cat) {
        pthread_mutex_unlock(&tn->mu);
        *out_data = NULL;
        *out_len = 0;
        return -1;
    }

    /* NewsArtListData wire format (maps to Go NewsArtListData.Read()):
     *   ID(4) + Count(4) + NameLen(1) + Name(n) + DescLen(1) + Desc(n)
     *   + [ArticleEntries...]
     *
     * Each article entry (maps to Go NewsArtList.Read()):
     *   ID(4) + TimeStamp(8) + ParentID(4) + Flags(4) +
     *   FlavorCount(2) + TitleLen(1) + Title(n) +
     *   PosterLen(1) + Poster(n) + FlavorNameLen(1) +
     *   FlavorName(n) + ArticleSize(2) */

    /* Count active articles */
    int active_count = 0;
    int i;
    for (i = 0; i < cat->article_count; i++) {
        if (cat->articles[i].active) active_count++;
    }

    /* Calculate size: header + articles */
    size_t header_size = 4 + 4 + 1 + 0 + 1 + 0; /* ID + Count + NameLen + DescLen */
    size_t articles_size = 0;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (!art->active) continue;
        articles_size += 4 + 8 + 4 + 4 + 2; /* fixed fields */
        articles_size += 1 + strlen(art->title);
        articles_size += 1 + strlen(art->poster);
        articles_size += 1 + strlen(NEWS_FLAVOR) + 2; /* flavor + article size */
    }

    size_t total = header_size + articles_size;
    uint8_t *buf = (uint8_t *)malloc(total > 0 ? total : 1);
    if (!buf) {
        pthread_mutex_unlock(&tn->mu);
        return -1;
    }

    size_t offset = 0;

    /* Header: ID (4 bytes, all zero for root) */
    memset(buf + offset, 0, 4); offset += 4;
    /* Count */
    hl_write_u32(buf + offset, (uint32_t)active_count); offset += 4;
    /* NameLen + Name (empty) */
    buf[offset++] = 0;
    /* DescLen + Desc (empty) */
    buf[offset++] = 0;

    /* Article entries */
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (!art->active) continue;

        size_t title_len = strlen(art->title);
        size_t poster_len = strlen(art->poster);
        size_t flavor_len = strlen(NEWS_FLAVOR);

        /* ID */
        hl_write_u32(buf + offset, art->id); offset += 4;
        /* Timestamp */
        memcpy(buf + offset, art->date, 8); offset += 8;
        /* ParentID */
        hl_write_u32(buf + offset, art->parent_id); offset += 4;
        /* Flags (4 bytes, all zero) */
        memset(buf + offset, 0, 4); offset += 4;
        /* FlavorCount = 1 */
        buf[offset++] = 0;
        buf[offset++] = 1;
        /* TitleLen + Title */
        buf[offset++] = (uint8_t)title_len;
        memcpy(buf + offset, art->title, title_len); offset += title_len;
        /* PosterLen + Poster */
        buf[offset++] = (uint8_t)poster_len;
        memcpy(buf + offset, art->poster, poster_len); offset += poster_len;
        /* FlavorNameLen + FlavorName */
        buf[offset++] = (uint8_t)flavor_len;
        memcpy(buf + offset, NEWS_FLAVOR, flavor_len); offset += flavor_len;
        /* ArticleSize (2 bytes) */
        buf[offset++] = (uint8_t)(art->data_len >> 8);
        buf[offset++] = (uint8_t)(art->data_len & 0xFF);
    }

    *out_data = buf;
    *out_len = offset;

    pthread_mutex_unlock(&tn->mu);
    return 0;
}

/* === Get a single article === */

int tn_get_article(mobius_threaded_news_t *tn,
                   const char *cat_name, uint32_t article_id,
                   uint8_t **out_title, uint16_t *title_len,
                   uint8_t **out_poster, uint16_t *poster_len,
                   uint8_t **out_data, uint16_t *data_len,
                   uint8_t *out_date)
{
    pthread_mutex_lock(&tn->mu);

    tn_category_t *cat = find_category(tn, cat_name);
    if (!cat) {
        pthread_mutex_unlock(&tn->mu);
        return -1;
    }

    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (art->active && art->id == article_id) {
            size_t tl = strlen(art->title);
            size_t pl = strlen(art->poster);

            *out_title = (uint8_t *)malloc(tl);
            memcpy(*out_title, art->title, tl);
            *title_len = (uint16_t)tl;

            *out_poster = (uint8_t *)malloc(pl);
            memcpy(*out_poster, art->poster, pl);
            *poster_len = (uint16_t)pl;

            *out_data = (uint8_t *)malloc(art->data_len);
            memcpy(*out_data, art->data, art->data_len);
            *data_len = art->data_len;

            memcpy(out_date, art->date, 8);

            pthread_mutex_unlock(&tn->mu);
            return 0;
        }
    }

    pthread_mutex_unlock(&tn->mu);
    return -1;
}

/* === Post article === */

int tn_post_article(mobius_threaded_news_t *tn,
                    const char *cat_name,
                    uint32_t parent_id,
                    const char *title,
                    const char *poster,
                    const char *data, uint16_t data_len)
{
    pthread_mutex_lock(&tn->mu);

    tn_category_t *cat = find_category(tn, cat_name);
    if (!cat || cat->article_count >= TN_MAX_ARTICLES) {
        pthread_mutex_unlock(&tn->mu);
        return -1;
    }

    tn_article_t *art = &cat->articles[cat->article_count];
    memset(art, 0, sizeof(*art));

    art->id = cat->next_article_id++;
    art->parent_id = parent_id;
    art->active = 1;

    strncpy(art->title, title, TN_MAX_TITLE_LEN - 1);
    strncpy(art->poster, poster, TN_MAX_POSTER_LEN - 1);

    if (data_len > TN_MAX_DATA_LEN) data_len = TN_MAX_DATA_LEN;
    memcpy(art->data, data, data_len);
    art->data_len = data_len;

    /* Set timestamp to now using Hotline date format */
    time_t now = time(NULL);
    hl_time_from_timet(art->date, now);

    cat->article_count++;

    pthread_mutex_unlock(&tn->mu);
    tn_save(tn);
    return 0;
}

/* === Create category === */

int tn_create_category(mobius_threaded_news_t *tn,
                       const char *name, const uint8_t type[2])
{
    pthread_mutex_lock(&tn->mu);

    if (tn->category_count >= TN_MAX_CATEGORIES) {
        pthread_mutex_unlock(&tn->mu);
        return -1;
    }

    /* Check if already exists */
    if (find_category(tn, name)) {
        pthread_mutex_unlock(&tn->mu);
        return 0; /* Already exists, not an error */
    }

    tn_category_t *cat = &tn->categories[tn->category_count];
    memset(cat, 0, sizeof(*cat));
    strncpy(cat->name, name, TN_MAX_NAME_LEN - 1);
    memcpy(cat->type, type, 2);
    cat->next_article_id = 1;
    tn->category_count++;

    pthread_mutex_unlock(&tn->mu);
    tn_save(tn);
    return 0;
}

/* === Delete news item === */

int tn_delete_news_item(mobius_threaded_news_t *tn, const char *name)
{
    pthread_mutex_lock(&tn->mu);

    int i;
    for (i = 0; i < tn->category_count; i++) {
        if (strcmp(tn->categories[i].name, name) == 0) {
            /* Shift remaining categories */
            int j;
            for (j = i; j < tn->category_count - 1; j++)
                tn->categories[j] = tn->categories[j + 1];
            tn->category_count--;
            pthread_mutex_unlock(&tn->mu);
            tn_save(tn);
            return 0;
        }
    }

    pthread_mutex_unlock(&tn->mu);
    return -1;
}

/* === Delete article === */

int tn_delete_article(mobius_threaded_news_t *tn,
                      const char *cat_name, uint32_t article_id)
{
    pthread_mutex_lock(&tn->mu);

    tn_category_t *cat = find_category(tn, cat_name);
    if (!cat) {
        pthread_mutex_unlock(&tn->mu);
        return -1;
    }

    int i;
    for (i = 0; i < cat->article_count; i++) {
        if (cat->articles[i].id == article_id) {
            cat->articles[i].active = 0;
            pthread_mutex_unlock(&tn->mu);
            tn_save(tn);
            return 0;
        }
    }

    pthread_mutex_unlock(&tn->mu);
    return -1;
}
