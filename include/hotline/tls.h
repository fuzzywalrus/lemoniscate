/*
 * tls.h - TLS transport abstraction
 *
 * Provides a connection wrapper that routes I/O through either
 * plain BSD sockets or TLS, and a server-side TLS context for
 * accepting TLS connections.
 *
 * macOS: SecureTransport backend (tls_sectransport.c)
 * Linux: OpenSSL backend (tls_openssl.c)
 */

#ifndef HOTLINE_TLS_H
#define HOTLINE_TLS_H

#include <stdint.h>
#include <stddef.h>
#include "hotline/platform/platform_tls.h"

/* --- Connection wrapper --- */

typedef struct hl_tls_conn {
    int fd;                    /* underlying socket (kept for event loop) */
    HL_TLS_CONN_FIELDS         /* platform-specific: SSLContextRef or SSL* */
} hl_tls_conn_t;

/* --- Server-side TLS context --- */

typedef struct {
    HL_TLS_CTX_FIELDS          /* platform-specific: CFArrayRef or SSL_CTX* */
    int enabled;               /* 1 if cert+key loaded successfully */
} hl_tls_server_ctx_t;

/*
 * hl_tls_server_ctx_init - Load certificate and private key from PEM files.
 *
 * Returns 0 on success, -1 on error.
 * On success, ctx->enabled is set to 1.
 */
int hl_tls_server_ctx_init(hl_tls_server_ctx_t *ctx,
                            const char *cert_path,
                            const char *key_path);

/* Free server TLS context resources */
void hl_tls_server_ctx_free(hl_tls_server_ctx_t *ctx);

/*
 * hl_tls_accept - Accept a TLS connection on an already-accepted socket fd.
 * Performs the TLS handshake. Returns a new hl_tls_conn_t on success,
 * NULL on handshake failure (closes fd on failure).
 */
hl_tls_conn_t *hl_tls_accept(hl_tls_server_ctx_t *ctx, int fd);

/*
 * hl_conn_wrap_plain - Wrap a plain (non-TLS) socket in a conn object.
 * Returns NULL on allocation failure.
 */
hl_tls_conn_t *hl_conn_wrap_plain(int fd);

/* --- Unified I/O --- */

/*
 * hl_conn_read - Single read (may return partial data).
 * Returns bytes read (>0), 0 on EOF, -1 on error.
 */
ssize_t hl_conn_read(hl_tls_conn_t *conn, uint8_t *buf, size_t len);

/*
 * hl_conn_read_full - Read exactly n bytes.
 * Returns 0 on success, -1 on error/EOF.
 */
int hl_conn_read_full(hl_tls_conn_t *conn, uint8_t *buf, size_t n);

/*
 * hl_conn_write_all - Write exactly n bytes.
 * Returns 0 on success, -1 on error.
 */
int hl_conn_write_all(hl_tls_conn_t *conn, const uint8_t *buf, size_t n);

/*
 * hl_conn_close - Close the connection (SSL shutdown + close fd).
 * Safe to call on NULL.
 */
void hl_conn_close(hl_tls_conn_t *conn);

/*
 * hl_conn_free - Free the connection wrapper without closing the fd.
 * Used when the fd is managed separately (e.g., kqueue cleanup).
 * Safe to call on NULL.
 */
void hl_conn_free(hl_tls_conn_t *conn);

#endif /* HOTLINE_TLS_H */
