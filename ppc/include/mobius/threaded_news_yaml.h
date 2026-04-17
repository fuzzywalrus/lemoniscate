/*
 * threaded_news_yaml.h - Threaded news manager
 *
 * Maps to: internal/mobius/threaded_news.go + hotline/news.go
 *
 * In-memory threaded news with a single default "General" category.
 * Articles are stored as a flat list per category.
 */

#ifndef MOBIUS_THREADED_NEWS_YAML_H
#define MOBIUS_THREADED_NEWS_YAML_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#define TN_MAX_ARTICLES    256
#define TN_MAX_CATEGORIES  16
#define TN_MAX_NAME_LEN    128
#define TN_MAX_TITLE_LEN   256
#define TN_MAX_POSTER_LEN  64
#define TN_MAX_DATA_LEN    8192

/* News article */
typedef struct {
    uint32_t  id;
    uint32_t  parent_id;
    char      title[TN_MAX_TITLE_LEN];
    char      poster[TN_MAX_POSTER_LEN];
    uint8_t   date[8];      /* Hotline date format */
    char      data[TN_MAX_DATA_LEN];
    uint16_t  data_len;
    int       active;
} tn_article_t;

/* News category (type=3) */
typedef struct {
    char          name[TN_MAX_NAME_LEN];
    uint8_t       type[2];   /* {0,2}=bundle, {0,3}=category */
    uint8_t       guid[16];
    uint8_t       add_sn[4];
    uint8_t       del_sn[4];
    tn_article_t  articles[TN_MAX_ARTICLES];
    int           article_count;
    uint32_t      next_article_id;
} tn_category_t;

/* Threaded news manager */
typedef struct mobius_threaded_news {
    char            file_path[1024];
    tn_category_t   categories[TN_MAX_CATEGORIES];
    int             category_count;
    pthread_mutex_t mu;
} mobius_threaded_news_t;

/* Create/free/persist */
mobius_threaded_news_t *mobius_threaded_news_new(const char *filepath);
void mobius_threaded_news_free(mobius_threaded_news_t *tn);
int tn_save(mobius_threaded_news_t *tn);
int tn_load(mobius_threaded_news_t *tn);

/* Get categories at path. Returns serialized NewsCategoryListData15 fields.
 * Caller must free *out_data. *out_len is total byte length.
 * *out_count is the number of category entries. */
int tn_get_categories(mobius_threaded_news_t *tn,
                      uint8_t **out_data, size_t *out_len,
                      int *out_count);

/* Get article list for a category. Returns serialized NewsArtListData.
 * Caller must free *out_data. */
int tn_get_article_list(mobius_threaded_news_t *tn,
                        const char *cat_name,
                        uint8_t **out_data, size_t *out_len);

/* Get a single article's data. Returns serialized fields.
 * Caller must free *out_data. */
int tn_get_article(mobius_threaded_news_t *tn,
                   const char *cat_name, uint32_t article_id,
                   uint8_t **out_title, uint16_t *title_len,
                   uint8_t **out_poster, uint16_t *poster_len,
                   uint8_t **out_data, uint16_t *data_len,
                   uint8_t *out_date);

/* Post a new article. Returns 0 on success. */
int tn_post_article(mobius_threaded_news_t *tn,
                    const char *cat_name,
                    uint32_t parent_id,
                    const char *title,
                    const char *poster,
                    const char *data, uint16_t data_len);

/* Create a new category. Returns 0 on success. */
int tn_create_category(mobius_threaded_news_t *tn,
                       const char *name, const uint8_t type[2]);

/* Delete a news item (category). Returns 0 on success. */
int tn_delete_news_item(mobius_threaded_news_t *tn, const char *name);

/* Delete an article. Returns 0 on success. */
int tn_delete_article(mobius_threaded_news_t *tn,
                      const char *cat_name, uint32_t article_id);

#endif /* MOBIUS_THREADED_NEWS_YAML_H */
