/*
 * dir_threaded_news.c - Directory-based threaded news (Janus-compatible)
 *
 * Loads/saves news articles as individual JSON files in a directory tree.
 * Populates the standard mobius_threaded_news_t struct so all existing
 * transaction handlers work unchanged.
 */

#include "mobius/dir_threaded_news.h"
#include "mobius/json_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

/* --- JSON parsing helpers (minimal, for known fields) --- */

static int json_get_int(const char *json, const char *key)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p += strlen(pattern);
    while (*p == ' ') p++;
    return atoi(p);
}

static void json_get_string(const char *json, const char *key,
                            char *out, size_t out_size)
{
    out[0] = '\0';
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return;
    p += strlen(pattern);

    /* Read until unescaped quote */
    size_t i = 0;
    while (*p && i < out_size - 1) {
        if (*p == '"') break;
        if (*p == '\\' && *(p + 1)) {
            switch (*(p + 1)) {
            case '"': out[i++] = '"'; p += 2; break;
            case '\\': out[i++] = '\\'; p += 2; break;
            case 'n': out[i++] = '\n'; p += 2; break;
            case 'r': out[i++] = '\r'; p += 2; break;
            case 't': out[i++] = '\t'; p += 2; break;
            default: out[i++] = *p; p++; break;
            }
        } else {
            out[i++] = *p;
            p++;
        }
    }
    out[i] = '\0';
}

static void json_get_guid(const char *json, uint8_t guid[16])
{
    memset(guid, 0, 16);
    const char *p = strstr(json, "\"guid\":[");
    if (!p) return;
    p += 8;
    int i;
    for (i = 0; i < 16 && *p; i++) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (*p == ']') break;
        guid[i] = (uint8_t)atoi(p);
        p = strchr(p, ',');
        if (p) p++; else break;
    }
}

static char *read_file_contents(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { *out_len = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); *out_len = 0; return NULL; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); *out_len = 0; return NULL; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

/* --- Load a category from its directory --- */

static int load_category(const char *dir_path, const char *dir_name,
                         tn_category_t *cat)
{
    memset(cat, 0, sizeof(*cat));
    strncpy(cat->name, dir_name, TN_MAX_NAME_LEN - 1);

    /* Read _meta.json */
    char meta_path[2048];
    snprintf(meta_path, sizeof(meta_path), "%s/_meta.json", dir_path);
    size_t meta_len;
    char *meta = read_file_contents(meta_path, &meta_len);
    if (meta) {
        char kind[32];
        json_get_string(meta, "kind", kind, sizeof(kind));
        if (strcmp(kind, "bundle") == 0) {
            cat->type[0] = 0; cat->type[1] = 2; /* bundle */
        } else {
            cat->type[0] = 0; cat->type[1] = 3; /* category */
        }
        json_get_guid(meta, cat->guid);
        /* add_sn, delete_sn stored as bytes in Hotline format */
        int add_sn = json_get_int(meta, "add_sn");
        int del_sn = json_get_int(meta, "delete_sn");
        cat->add_sn[0] = (uint8_t)((add_sn >> 24) & 0xFF);
        cat->add_sn[1] = (uint8_t)((add_sn >> 16) & 0xFF);
        cat->add_sn[2] = (uint8_t)((add_sn >> 8) & 0xFF);
        cat->add_sn[3] = (uint8_t)(add_sn & 0xFF);
        cat->del_sn[0] = (uint8_t)((del_sn >> 24) & 0xFF);
        cat->del_sn[1] = (uint8_t)((del_sn >> 16) & 0xFF);
        cat->del_sn[2] = (uint8_t)((del_sn >> 8) & 0xFF);
        cat->del_sn[3] = (uint8_t)(del_sn & 0xFF);
        free(meta);
    } else {
        /* No meta — default to category */
        cat->type[0] = 0; cat->type[1] = 3;
    }

    /* Load articles (NNNN.json files) */
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    uint32_t max_id = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' || ent->d_name[0] == '_') continue;

        /* Check if it's a .json file (not a subdirectory) */
        size_t nlen = strlen(ent->d_name);
        if (nlen < 6 || strcmp(ent->d_name + nlen - 5, ".json") != 0)
            continue;

        char art_path[2048];
        snprintf(art_path, sizeof(art_path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(art_path, &st) != 0 || S_ISDIR(st.st_mode))
            continue;

        if (cat->article_count >= TN_MAX_ARTICLES)
            break;

        size_t art_len;
        char *art_json = read_file_contents(art_path, &art_len);
        if (!art_json) continue;

        tn_article_t *art = &cat->articles[cat->article_count];
        memset(art, 0, sizeof(*art));

        art->id = (uint32_t)json_get_int(art_json, "id");
        art->parent_id = (uint32_t)json_get_int(art_json, "parent_id");
        json_get_string(art_json, "title", art->title, TN_MAX_TITLE_LEN);
        json_get_string(art_json, "poster", art->poster, TN_MAX_POSTER_LEN);

        /* Parse body */
        char body[TN_MAX_DATA_LEN];
        json_get_string(art_json, "body", body, sizeof(body));
        strncpy(art->data, body, TN_MAX_DATA_LEN - 1);
        art->data_len = (uint16_t)strlen(art->data);

        /* Parse ISO date to Hotline format */
        char date_str[64];
        json_get_string(art_json, "date", date_str, sizeof(date_str));
        if (date_str[0]) {
            int yr = 0, mo = 0, dy = 0, hr = 0, mi = 0, se = 0;
            sscanf(date_str, "%d-%d-%dT%d:%d:%d", &yr, &mo, &dy, &hr, &mi, &se);
            struct tm tm_val;
            memset(&tm_val, 0, sizeof(tm_val));
            tm_val.tm_year = yr - 1900;
            tm_val.tm_mon = mo - 1;
            tm_val.tm_mday = dy;
            tm_val.tm_hour = hr;
            tm_val.tm_min = mi;
            tm_val.tm_sec = se;
            time_t unix_t = timegm(&tm_val);
            /* Convert to Mac epoch (seconds since 1904-01-01) */
            uint32_t mac_secs = (uint32_t)(unix_t + 2082844800LL);
            uint16_t year = (uint16_t)yr;
            art->date[0] = (uint8_t)(year >> 8);
            art->date[1] = (uint8_t)(year & 0xFF);
            art->date[2] = 0; art->date[3] = 0; /* milliseconds */
            art->date[4] = (uint8_t)((mac_secs >> 24) & 0xFF);
            art->date[5] = (uint8_t)((mac_secs >> 16) & 0xFF);
            art->date[6] = (uint8_t)((mac_secs >> 8) & 0xFF);
            art->date[7] = (uint8_t)(mac_secs & 0xFF);
        }

        art->active = 1;
        if (art->id > max_id) max_id = art->id;
        cat->article_count++;

        free(art_json);
    }
    closedir(d);

    cat->next_article_id = max_id + 1;
    return 0;
}

/* --- Public API --- */

mobius_threaded_news_t *mobius_dir_news_new(const char *base_path)
{
    mobius_threaded_news_t *tn = (mobius_threaded_news_t *)calloc(1, sizeof(*tn));
    if (!tn) return NULL;

    strncpy(tn->file_path, base_path, sizeof(tn->file_path) - 1);
    pthread_mutex_init(&tn->mu, NULL);

    /* Scan News/ directory for categories */
    DIR *d = opendir(base_path);
    if (!d) return tn;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (tn->category_count >= TN_MAX_CATEGORIES) break;

        char cat_path[2048];
        snprintf(cat_path, sizeof(cat_path), "%s/%s", base_path, ent->d_name);

        struct stat st;
        if (stat(cat_path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        load_category(cat_path, ent->d_name,
                      &tn->categories[tn->category_count]);
        tn->category_count++;
    }
    closedir(d);

    return tn;
}

/* --- Save individual article to disk --- */

int tn_dir_save_article(const char *base_path, const char *cat_name,
                        const tn_article_t *art)
{
    char dir_path[2048];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", base_path, cat_name);

    /* Ensure directory exists */
    mkdir(dir_path, 0755);

    /* Convert Hotline date to ISO */
    char date_str[64] = "1970-01-01T00:00:00Z";
    uint32_t secs = ((uint32_t)art->date[4] << 24) |
                    ((uint32_t)art->date[5] << 16) |
                    ((uint32_t)art->date[6] << 8) |
                     (uint32_t)art->date[7];
    if (secs > 0) {
        time_t unix_secs = (time_t)secs - 2082844800LL;
        struct tm *tm = gmtime(&unix_secs);
        if (tm) {
            snprintf(date_str, sizeof(date_str),
                     "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min, tm->tm_sec);
        }
    }

    /* Build JSON */
    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "{\n");
    json_buf_printf(&buf, "  \"id\": %u,\n", art->id);
    json_buf_append_str(&buf, "  ");
    json_buf_add_string(&buf, "title", art->title);
    json_buf_append_str(&buf, ",\n  ");
    json_buf_add_string(&buf, "poster", art->poster);
    json_buf_append_str(&buf, ",\n  ");
    json_buf_add_string(&buf, "date", date_str);
    json_buf_append_str(&buf, ",\n");
    json_buf_printf(&buf, "  \"parent_id\": %u,\n", art->parent_id);
    json_buf_append_str(&buf, "  ");
    json_buf_add_string(&buf, "body", art->data);
    json_buf_append_str(&buf, "\n}\n");

    /* Write to NNNN.json */
    char file_path[2100];
    snprintf(file_path, sizeof(file_path), "%s/%04u.json", dir_path, art->id);

    /* Atomic write: tmp + rename */
    char tmp_path[2110];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);
    FILE *f = fopen(tmp_path, "w");
    if (f && buf.data) {
        fwrite(buf.data, 1, buf.len, f);
        fclose(f);
        rename(tmp_path, file_path);
    }

    json_buf_free(&buf);
    return 0;
}

/* --- Save category metadata --- */

int tn_dir_save_meta(const char *base_path, const char *cat_name,
                     const tn_category_t *cat)
{
    char dir_path[2048];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", base_path, cat_name);
    mkdir(dir_path, 0755);

    const char *kind = (cat->type[1] == 2) ? "bundle" : "category";

    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "{\n");
    json_buf_append_str(&buf, "  ");
    json_buf_add_string(&buf, "name", cat->name);
    json_buf_append_str(&buf, ",\n  ");
    json_buf_add_string(&buf, "kind", kind);
    json_buf_append_str(&buf, ",\n  \"guid\": [");

    int i;
    for (i = 0; i < 16; i++) {
        if (i > 0) json_buf_append_str(&buf, ", ");
        json_buf_printf(&buf, "%u", cat->guid[i]);
    }
    json_buf_append_str(&buf, "],\n");

    int add_sn = ((int)cat->add_sn[0] << 24) | ((int)cat->add_sn[1] << 16) |
                 ((int)cat->add_sn[2] << 8) | (int)cat->add_sn[3];
    int del_sn = ((int)cat->del_sn[0] << 24) | ((int)cat->del_sn[1] << 16) |
                 ((int)cat->del_sn[2] << 8) | (int)cat->del_sn[3];
    json_buf_printf(&buf, "  \"add_sn\": %d,\n", add_sn);
    json_buf_printf(&buf, "  \"delete_sn\": %d\n", del_sn);
    json_buf_append_str(&buf, "}\n");

    char meta_path[2100];
    snprintf(meta_path, sizeof(meta_path), "%s/_meta.json", dir_path);
    char tmp_path[2110];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", meta_path);

    FILE *f = fopen(tmp_path, "w");
    if (f && buf.data) {
        fwrite(buf.data, 1, buf.len, f);
        fclose(f);
        rename(tmp_path, meta_path);
    }

    json_buf_free(&buf);
    return 0;
}

/* --- Full save (all categories and articles) --- */

int tn_dir_save(mobius_threaded_news_t *tn)
{
    if (!tn || tn->file_path[0] == '\0') return -1;

    int i, j;
    for (i = 0; i < tn->category_count; i++) {
        tn_category_t *cat = &tn->categories[i];
        tn_dir_save_meta(tn->file_path, cat->name, cat);

        for (j = 0; j < cat->article_count; j++) {
            if (cat->articles[j].active) {
                tn_dir_save_article(tn->file_path, cat->name, &cat->articles[j]);
            }
        }
    }
    return 0;
}

/* --- Migration from ThreadedNews.yaml --- */

int mobius_migrate_yaml_to_dir(const char *yaml_path, const char *news_dir)
{
    /* Load the YAML file using existing loader */
    mobius_threaded_news_t *tn = mobius_threaded_news_new(NULL);
    if (!tn) return -1;

    strncpy(tn->file_path, yaml_path, sizeof(tn->file_path) - 1);
    int rc = tn_load(tn);
    if (rc != 0) {
        mobius_threaded_news_free(tn);
        return -1;
    }

    /* Create News/ directory */
    mkdir(news_dir, 0755);

    /* Write all categories and articles as JSON files */
    strncpy(tn->file_path, news_dir, sizeof(tn->file_path) - 1);
    tn_dir_save(tn);

    mobius_threaded_news_free(tn);

    /* Rename old YAML to .legacy */
    char legacy[1100];
    snprintf(legacy, sizeof(legacy), "%s.legacy", yaml_path);
    rename(yaml_path, legacy);

    return 0;
}
