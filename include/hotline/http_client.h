/*
 * http_client.h - Minimal HTTP POST client for Mnemosyne sync
 *
 * Blocking HTTP/1.1 POST over plain TCP. Designed for the Mnemosyne
 * sync protocol — small JSON payloads, short timeouts, no keepalive.
 */

#ifndef HOTLINE_HTTP_CLIENT_H
#define HOTLINE_HTTP_CLIENT_H

#include <stddef.h>

/* Parsed URL components */
typedef struct {
    char host[256];
    int  port;         /* default 80 */
    char path[512];    /* includes leading '/' */
} hl_parsed_url_t;

/*
 * hl_http_parse_url - Parse an HTTP URL into components.
 * Returns 0 on success, -1 on parse error.
 */
int hl_http_parse_url(const char *url, hl_parsed_url_t *out);

/*
 * hl_http_url_with_api_key - Build a full URL path with ?api_key= appended.
 * Writes into out_buf (up to out_size bytes). Returns 0 on success.
 */
int hl_http_url_with_api_key(const hl_parsed_url_t *url, const char *api_key,
                             char *out_buf, size_t out_size);

/*
 * hl_http_post - Send an HTTP POST request.
 *
 * host:         Hostname or IP to connect to
 * port:         TCP port
 * path:         URL path (e.g., "/api/v1/sync/heartbeat?api_key=msv_...")
 * body:         POST body (JSON)
 * body_len:     Length of body
 * content_type: Content-Type header value (e.g., "application/json")
 * connect_timeout_ms: TCP connect timeout in milliseconds
 * read_timeout_ms:    Response read timeout in milliseconds
 *
 * Returns the HTTP status code (e.g., 200) on success, or -1 on error
 * (connection refused, timeout, parse failure).
 */
int hl_http_post(const char *host, int port, const char *path,
                 const char *body, size_t body_len,
                 const char *content_type,
                 int connect_timeout_ms, int read_timeout_ms);

/*
 * hl_http_post_to_ip - Like hl_http_post but connects to a cached IP address
 * instead of resolving the hostname. The Host header still uses the hostname.
 */
int hl_http_post_to_ip(const char *ip, const char *hostname, int port,
                       const char *path, const char *body, size_t body_len,
                       const char *content_type,
                       int connect_timeout_ms, int read_timeout_ms);

/*
 * hl_http_get - Send an HTTP GET request and read the response body.
 *
 * host:         Hostname to connect to (also used for DNS resolution)
 * port:         TCP port
 * path:         URL path with query string
 * out_body:     Caller-provided buffer to receive response body
 * out_body_size: Size of out_body buffer
 * connect_timeout_ms: TCP connect timeout in milliseconds
 * read_timeout_ms:    Response read timeout in milliseconds
 *
 * Returns the HTTP status code on success, -1 on error.
 * out_body is null-terminated on success.
 */
int hl_http_get(const char *host, int port, const char *path,
                char *out_body, size_t out_body_size,
                int connect_timeout_ms, int read_timeout_ms);

#endif /* HOTLINE_HTTP_CLIENT_H */
