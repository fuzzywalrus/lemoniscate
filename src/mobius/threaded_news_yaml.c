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
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Wire format constants */
static const uint8_t NEWS_BUNDLE[2]   = {0x00, 0x02};
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

    /* NewsArtListData wire format:
     * For each article:
     *   ID(4) + TimeStamp(8) + ParentID(4) + Flags(4) +
     *   FlavorCount(2) + TitleLen(1) + Title(n) +
     *   PosterLen(1) + Poster(n) + FlavorNameLen(1) +
     *   FlavorName(n) + ArticleSize(2) */

    /* Calculate size */
    size_t total = 0;
    int i;
    for (i = 0; i < cat->article_count; i++) {
        tn_article_t *art = &cat->articles[i];
        if (!art->active) continue;
        total += 4 + 8 + 4 + 4 + 2; /* fixed fields */
        total += 1 + strlen(art->title);
        total += 1 + strlen(art->poster);
        total += 1 + strlen(NEWS_FLAVOR) + 2; /* flavor + article size */
    }

    uint8_t *buf = (uint8_t *)malloc(total > 0 ? total : 1);
    if (!buf) {
        pthread_mutex_unlock(&tn->mu);
        return -1;
    }

    size_t offset = 0;
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
    hl_time_to_hotline(now, art->date);

    cat->article_count++;

    pthread_mutex_unlock(&tn->mu);
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
            return 0;
        }
    }

    pthread_mutex_unlock(&tn->mu);
    return -1;
}
