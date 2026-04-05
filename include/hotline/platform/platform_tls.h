/*
 * platform_tls.h - Platform-specific TLS type definitions
 *
 * This header provides the platform-specific struct fields for
 * hl_tls_conn_t and hl_tls_server_ctx_t. The public API remains
 * in hotline/tls.h and is platform-independent.
 *
 * macOS: SecureTransport (tls_sectransport.c)
 * Linux: OpenSSL (tls_openssl.c)
 */

#ifndef HOTLINE_PLATFORM_TLS_H
#define HOTLINE_PLATFORM_TLS_H

#ifdef __APPLE__

#include <Security/Security.h>
#include <Security/SecureTransport.h>

#define HL_TLS_CONN_FIELDS \
    SSLContextRef ssl_ctx;

#define HL_TLS_CTX_FIELDS \
    CFArrayRef identity_certs;

#elif defined(__linux__)

#include <openssl/ssl.h>

#define HL_TLS_CONN_FIELDS \
    SSL *ssl;

#define HL_TLS_CTX_FIELDS \
    SSL_CTX *ssl_ctx;

#else

/* Stub for unsupported platforms */
#define HL_TLS_CONN_FIELDS \
    void *ssl_ctx;

#define HL_TLS_CTX_FIELDS \
    void *identity_certs;

#endif

#endif /* HOTLINE_PLATFORM_TLS_H */
