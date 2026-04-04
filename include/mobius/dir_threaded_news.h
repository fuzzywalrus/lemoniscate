/*
 * dir_threaded_news.h - Directory-based threaded news (Janus-compatible)
 *
 * Stores news as a directory tree:
 *   News/
 *     Category/
 *       _meta.json    {name, kind, guid, add_sn, delete_sn}
 *       0001.json     {id, title, poster, date, parent_id, body}
 *     Bundle/
 *       _meta.json    {name, kind:"bundle", ...}
 *       SubCat/
 *         _meta.json
 *         0001.json
 *
 * Provides the same mobius_threaded_news_t interface for transparent swap.
 */

#ifndef MOBIUS_DIR_THREADED_NEWS_H
#define MOBIUS_DIR_THREADED_NEWS_H

#include "mobius/threaded_news_yaml.h"

/*
 * mobius_dir_news_new - Load threaded news from directory tree.
 *
 * base_path: path to the News/ directory.
 * Returns a mobius_threaded_news_t* populated from the directory tree.
 * The file_path field is set to base_path for save operations.
 */
mobius_threaded_news_t *mobius_dir_news_new(const char *base_path);

/*
 * tn_dir_save - Save a single article or category change to disk.
 * Called after tn_post_article, tn_create_category, etc.
 * Writes only the changed file, not the entire tree.
 */
int tn_dir_save(mobius_threaded_news_t *tn);

/*
 * tn_dir_save_article - Save a single article file.
 */
int tn_dir_save_article(const char *base_path, const char *cat_name,
                        const tn_article_t *art);

/*
 * tn_dir_save_meta - Save category _meta.json.
 */
int tn_dir_save_meta(const char *base_path, const char *cat_name,
                     const tn_category_t *cat);

/*
 * mobius_migrate_yaml_to_dir - Migrate ThreadedNews.yaml to News/ directory.
 *
 * Reads the YAML file, creates directory structure with JSON files,
 * renames old file to .legacy. Returns 0 on success.
 */
int mobius_migrate_yaml_to_dir(const char *yaml_path, const char *news_dir);

#endif /* MOBIUS_DIR_THREADED_NEWS_H */
