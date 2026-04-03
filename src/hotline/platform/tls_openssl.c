/*
 * tls_openssl.c - Linux TLS backend using OpenSSL
 *
 * Implements the same API as tls_sectransport.c using OpenSSL's
 * SSL_CTX/SSL APIs. PEM loading is straightforward compared to
 * the SecureTransport keychain dance.
 *
 * This file is only compiled on Linux.
 */

#include "hotline/tls.h"

#ifdef __linux__

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

int hl_tls_server_ctx_init(hl_tls_server_ctx_t *ctx,
                            const char *cert_path,
                            const char *key_path)
{
    memset(ctx, 0, sizeof(*ctx));

    const SSL_METHOD *method = TLS_server_method();
    ctx->ssl_ctx = SSL_CTX_new(method);
    if (!ctx->ssl_ctx) {
        fprintf(stderr, "[TLS] Failed to create SSL_CTX\n");
        return -1;
    }

    if (SSL_CTX_use_certificate_chain_file(ctx->ssl_ctx, cert_path) != 1) {
        fprintf(stderr, "[TLS] Failed to load certificate: %s\n", cert_path);
        SSL_CTX_free(ctx->ssl_ctx);
        ctx->ssl_ctx = NULL;
        return -1;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx->ssl_ctx, key_path, SSL_FILETYPE_PEM) != 1) {
        fprintf(stderr, "[TLS] Failed to load private key: %s\n", key_path);
        SSL_CTX_free(ctx->ssl_ctx);
        ctx->ssl_ctx = NULL;
        return -1;
    }

    if (SSL_CTX_check_private_key(ctx->ssl_ctx) != 1) {
        fprintf(stderr, "[TLS] Private key does not match certificate\n");
        SSL_CTX_free(ctx->ssl_ctx);
        ctx->ssl_ctx = NULL;
        return -1;
    }

    ctx->enabled = 1;
    return 0;
}

void hl_tls_server_ctx_free(hl_tls_server_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->ssl_ctx) {
        SSL_CTX_free(ctx->ssl_ctx);
        ctx->ssl_ctx = NULL;
    }
    ctx->enabled = 0;
}

hl_tls_conn_t *hl_tls_accept(hl_tls_server_ctx_t *ctx, int fd)
{
    if (!ctx || !ctx->ssl_ctx) {
        close(fd);
        return NULL;
    }

    SSL *ssl = SSL_new(ctx->ssl_ctx);
    if (!ssl) {
        close(fd);
        return NULL;
    }

    SSL_set_fd(ssl, fd);

    int ret = SSL_accept(ssl);
    if (ret != 1) {
        SSL_free(ssl);
        close(fd);
        return NULL;
    }

    hl_tls_conn_t *conn = calloc(1, sizeof(hl_tls_conn_t));
    if (!conn) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(fd);
        return NULL;
    }

    conn->fd = fd;
    conn->ssl = ssl;
    return conn;
}

hl_tls_conn_t *hl_conn_wrap_plain(int fd)
{
    hl_tls_conn_t *conn = calloc(1, sizeof(hl_tls_conn_t));
    if (!conn) return NULL;
    conn->fd = fd;
    conn->ssl = NULL;
    return conn;
}

ssize_t hl_conn_read(hl_tls_conn_t *conn, uint8_t *buf, size_t len)
{
    if (!conn) return -1;

    if (conn->ssl) {
        int r = SSL_read(conn->ssl, buf, (int)len);
        if (r > 0) return r;
        int err = SSL_get_error(conn->ssl, r);
        if (err == SSL_ERROR_ZERO_RETURN) return 0; /* EOF */
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        return -1;
    }

    ssize_t r;
    do { r = read(conn->fd, buf, len); } while (r < 0 && errno == EINTR);
    return r;
}

int hl_conn_read_full(hl_tls_conn_t *conn, uint8_t *buf, size_t n)
{
    if (!conn) return -1;
    size_t total = 0;
    while (total < n) {
        ssize_t r;
        if (conn->ssl) {
            r = SSL_read(conn->ssl, buf + total, (int)(n - total));
            if (r <= 0) {
                int err = SSL_get_error(conn->ssl, (int)r);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                    continue;
                return -1;
            }
        } else {
            r = read(conn->fd, buf + total, n - total);
            if (r < 0) { if (errno == EINTR) continue; return -1; }
            if (r == 0) return -1;
        }
        total += (size_t)r;
    }
    return 0;
}

int hl_conn_write_all(hl_tls_conn_t *conn, const uint8_t *buf, size_t n)
{
    if (!conn) return -1;
    size_t total = 0;
    while (total < n) {
        ssize_t w;
        if (conn->ssl) {
            w = SSL_write(conn->ssl, buf + total, (int)(n - total));
            if (w <= 0) {
                int err = SSL_get_error(conn->ssl, (int)w);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                    continue;
                return -1;
            }
        } else {
            w = write(conn->fd, buf + total, n - total);
            if (w < 0) { if (errno == EINTR) continue; return -1; }
            if (w == 0) return -1;
        }
        total += (size_t)w;
    }
    return 0;
}

void hl_conn_close(hl_tls_conn_t *conn)
{
    if (!conn) return;
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    if (conn->fd >= 0) close(conn->fd);
    free(conn);
}

void hl_conn_free(hl_tls_conn_t *conn)
{
    if (!conn) return;
    if (conn->ssl) {
        SSL_free(conn->ssl);
        conn->ssl = NULL;
    }
    free(conn);
}

#endif /* __linux__ */
