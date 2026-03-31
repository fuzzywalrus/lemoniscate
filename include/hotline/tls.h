/*
 * tls.h - TLS transport abstraction (SecureTransport)
 *
 * Provides a connection wrapper that routes I/O through either
 * plain BSD sockets or SecureTransport SSL, and a server-side
 * TLS context for accepting TLS connections.
 *
 * Tiger 10.4 compatibility:
 *   Uses SSLNewContext/SSLDisposeContext (not SSLCreateContext which is 10.8+).
 *   Uses SecKeychainItemImport (not SecItemImport which is 10.7+).
 *   Uses SecIdentitySearchCreate (not SecIdentityCreateWithCertificate, 10.5+).
 *   All APIs used here are available in the Tiger 10.4 Security framework.
 *
 * Maps to: Go crypto/tls usage in hotline/server.go
 * Remove this file and tls.c if TLS support is dropped.
 */

#ifndef HOTLINE_TLS_H
#define HOTLINE_TLS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h> /* ssize_t */

#ifdef __APPLE__
#include <Security/Security.h>
#include <Security/SecureTransport.h>
#endif

/* --- Connection wrapper --- */

typedef struct hl_tls_conn {
    int fd;                    /* underlying socket (kept for kqueue) */
#ifdef __APPLE__
    SSLContextRef ssl_ctx;     /* NULL for plain connections */
#else
    void *ssl_ctx;
#endif
} hl_tls_conn_t;

/* --- Server-side TLS context --- */

typedef struct {
#ifdef __APPLE__
    CFArrayRef identity_certs; /* SecIdentity + optional chain certs */
#else
    void *identity_certs;
#endif
    int enabled;               /* 1 if cert+key loaded successfully */
} hl_tls_server_ctx_t;

/*
 * hl_tls_server_ctx_init - Load certificate and private key from PEM files.
 * Maps to: Go tls.LoadX509KeyPair() in main.go
 *
 * On Tiger, imports PEM into a temporary keychain and builds a SecIdentity.
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
