/*
 * jsonl_message_board.h - JSONL-based message board storage
 *
 * Stores posts as newline-delimited JSON (Janus-compatible format).
 * Each line: {"id":N,"nick":"...","login":"...","body":"...","ts":"ISO8601"}
 *
 * Implements the same interface as mobius_flat_news_t so the server
 * can use either backend transparently.
 */

#ifndef MOBIUS_JSONL_MESSAGE_BOARD_H
#define MOBIUS_JSONL_MESSAGE_BOARD_H

#include "mobius/flat_news.h"
#include <stddef.h>
#include <stdint.h>

/*
 * A single message board post (parsed from JSONL).
 */
typedef struct {
    int         id;
    char        nick[128];
    char        login[128];
    char       *body;       /* heap-allocated */
    char        ts[64];     /* ISO 8601 timestamp */
} mb_post_t;

/*
 * mobius_jsonl_news_new - Create a JSONL message board from file.
 *
 * Returns a mobius_flat_news_t* that stores data internally as JSONL
 * but presents the same interface (data/prepend/free).
 * The 'data' function returns formatted text for the Hotline wire protocol.
 */
mobius_flat_news_t *mobius_jsonl_news_new(const char *path);

/*
 * mobius_jsonl_post_count - Get the number of structured posts.
 * Returns 0 if the backend is flat text (not JSONL).
 */
int mobius_jsonl_post_count(mobius_flat_news_t *fn);

/*
 * mobius_jsonl_get_posts - Get structured posts for Mnemosyne sync.
 * Caller must free each post's body and the array itself.
 * Returns number of posts, or -1 on error.
 */
int mobius_jsonl_get_posts(mobius_flat_news_t *fn,
                           mb_post_t **out_posts, int *out_count);

/*
 * mobius_jsonl_free_posts - Free an array returned by mobius_jsonl_get_posts.
 */
void mobius_jsonl_free_posts(mb_post_t *posts, int count);

/*
 * mobius_migrate_flat_to_jsonl - Migrate MessageBoard.txt to MessageBoard.jsonl.
 *
 * Parses the flat text file, extracts posts, writes JSONL, renames
 * old file to .legacy. Returns 0 on success.
 */
int mobius_migrate_flat_to_jsonl(const char *txt_path, const char *jsonl_path);

#endif /* MOBIUS_JSONL_MESSAGE_BOARD_H */
