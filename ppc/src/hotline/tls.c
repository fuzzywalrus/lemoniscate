/*
 * tls.c - TLS transport abstraction (SecureTransport)
 *
 * Tiger 10.4 compatible implementation using:
 *   - SSLNewContext / SSLDisposeContext  (not SSLCreateContext, 10.8+)
 *   - SecKeychainItemImport             (not SecItemImport, 10.7+)
 *   - SecIdentitySearchCreate           (not SecIdentityCreateWithCertificate, 10.5+)
 *
 * Tiger's SecureTransport supports up to TLS 1.0. Modern Hotline clients
 * like Navigator should accept TLS 1.0 when connecting to a retro server.
 * The server also accepts plaintext + HOPE on the standard port, so TLS
 * is purely additive — clients that can't negotiate TLS 1.0 fall back.
 *
 * Maps to: Go crypto/tls usage in hotline/server.go
 * Remove this file if TLS support is dropped.
 */

#include "hotline/tls.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>

#ifdef __APPLE__

/* errSecIO was introduced after Leopard — define it as classic Mac ioErr (-36) */
#ifndef errSecIO
#define errSecIO -36
#endif

/* --- SecureTransport I/O callbacks --- */

static OSStatus tls_read_func(SSLConnectionRef connection,
                               void *data, size_t *dataLength)
{
    int fd = *(const int *)connection;
    size_t requested = *dataLength;
    ssize_t r = read(fd, data, requested);

    if (r < 0) {
        *dataLength = 0;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return errSSLWouldBlock;
        return errSecIO;
    }
    if (r == 0) {
        *dataLength = 0;
        return errSSLClosedGraceful;
    }

    *dataLength = (size_t)r;
    if ((size_t)r < requested)
        return errSSLWouldBlock;
    return noErr;
}

static OSStatus tls_write_func(SSLConnectionRef connection,
                                const void *data, size_t *dataLength)
{
    int fd = *(const int *)connection;
    size_t requested = *dataLength;
    ssize_t w = write(fd, data, requested);

    if (w < 0) {
        *dataLength = 0;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return errSSLWouldBlock;
        return errSecIO;
    }

    *dataLength = (size_t)w;
    if ((size_t)w < requested)
        return errSSLWouldBlock;
    return noErr;
}

/* --- PEM loading (Tiger-compatible) --- */

/* Read an entire file into a malloc'd buffer. Caller must free. */
static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return NULL; }

    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (n != (size_t)len) { free(buf); return NULL; }

    *out_len = (size_t)len;
    return buf;
}

/* Import PEM items from a file using SecKeychainItemImport (Tiger API).
 * On Tiger, SecKeychainItemImport is the equivalent of SecItemImport (10.7+).
 * Returns a CFArrayRef of imported items (caller must CFRelease).
 * Returns NULL on failure. */
static CFArrayRef import_pem_file(const char *path,
                                   SecExternalItemType expected_type,
                                   SecKeychainRef keychain)
{
    size_t data_len = 0;
    uint8_t *data = read_file(path, &data_len);
    if (!data) return NULL;

    CFDataRef cf_data = CFDataCreate(NULL, data, (CFIndex)data_len);
    free(data);
    if (!cf_data) return NULL;

    SecExternalFormat format = kSecFormatPEMSequence;
    SecExternalItemType type = expected_type;
    CFArrayRef items = NULL;

    /* SecKeychainItemImport — available 10.0+, deprecated 10.7.
     * On Tiger this is the current API. The key params struct is
     * SecKeyImportExportParameters (not SecItemImportExportKeyParameters). */
    SecKeyImportExportParameters key_params;
    memset(&key_params, 0, sizeof(key_params));
    key_params.version = SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION;
    key_params.passphrase = CFSTR("");

    OSStatus status = SecKeychainItemImport(cf_data, NULL, &format, &type,
                                             0, &key_params, keychain,
                                             &items);
    CFRelease(cf_data);
    /* Note: key_params.passphrase is CFSTR(""), a compile-time constant —
     * do NOT CFRelease it (CFSTR objects are not refcounted). */

    if (status != noErr || !items) {
        if (items) CFRelease(items);
        return NULL;
    }

    return items;
}

/* Build a SecIdentityRef from separate cert and key PEM files.
 *
 * Tiger strategy:
 *   1. Create a temporary keychain
 *   2. Import private key into it via SecKeychainItemImport
 *   3. Import certificate into it via SecKeychainItemImport
 *   4. Use SecIdentitySearchCreate to find the cert+key identity pair
 *   5. Build the CFArray for SSLSetCertificate
 */
static int load_identity_from_pem(const char *cert_path,
                                   const char *key_path,
                                   CFArrayRef *out_identity_certs)
{
    *out_identity_certs = NULL;

    /* Read keychain password from keychain.pass alongside the cert.
     * Falls back to a default if the file doesn't exist. */
    char kc_pass[64] = "lemoniscate-default-kc";
    size_t kc_pass_len = strlen(kc_pass);
    {
        /* Derive pass file path from cert_path directory */
        char pass_path[2048];
        const char *slash = strrchr(cert_path, '/');
        if (slash) {
            size_t dir_len = (size_t)(slash - cert_path);
            if (dir_len >= sizeof(pass_path)) dir_len = sizeof(pass_path) - 1;
            memcpy(pass_path, cert_path, dir_len);
            pass_path[dir_len] = '\0';
            strncat(pass_path, "/keychain.pass", sizeof(pass_path) - dir_len - 1);
        } else {
            strncpy(pass_path, "keychain.pass", sizeof(pass_path) - 1);
        }
        FILE *pf = fopen(pass_path, "r");
        if (pf) {
            char buf[64];
            if (fgets(buf, sizeof(buf), pf)) {
                /* Trim newline */
                size_t len = strlen(buf);
                while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
                    buf[--len] = '\0';
                if (len > 0) {
                    memcpy(kc_pass, buf, len + 1);
                    kc_pass_len = len;
                }
            }
            fclose(pf);
        }
    }

    /* Create a temporary keychain for this server process */
    SecKeychainRef temp_keychain = NULL;
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/lemoniscate-tls-%d.keychain",
             getpid());

    /* Remove stale keychain file if it exists */
    unlink(temp_path);

    OSStatus status = SecKeychainCreate(temp_path, (UInt32)kc_pass_len, kc_pass,
                                         FALSE, NULL, &temp_keychain);
    if (status != noErr) {
        fprintf(stderr, "[TLS] Failed to create temporary keychain: %d\n",
                (int)status);
        return -1;
    }

    /* Unlock the keychain and disable auto-lock so SecureTransport can
     * access the private key during TLS handshakes without prompting.
     * Also add it to the keychain search list so the Security framework
     * can find identities in it. */
    SecKeychainUnlock(temp_keychain, (UInt32)kc_pass_len, kc_pass, TRUE);

    {
        SecKeychainSettings settings;
        settings.version = SEC_KEYCHAIN_SETTINGS_VERS1;
        settings.lockOnSleep = FALSE;
        settings.useLockInterval = FALSE;
        settings.lockInterval = 0;
        SecKeychainSetSettings(temp_keychain, &settings);
    }

    /* Add temp keychain to search list so SecIdentitySearch finds it */
    {
        CFArrayRef existing = NULL;
        SecKeychainCopySearchList(&existing);
        CFMutableArrayRef newList;
        if (existing) {
            newList = CFArrayCreateMutableCopy(NULL, 0, existing);
            CFRelease(existing);
        } else {
            newList = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
        }
        CFArrayAppendValue(newList, temp_keychain);
        SecKeychainSetSearchList(newList);
        CFRelease(newList);
    }

    /* Import private key into the temp keychain */
    CFArrayRef key_items = import_pem_file(key_path, kSecItemTypePrivateKey,
                                            temp_keychain);
    if (!key_items || CFArrayGetCount(key_items) == 0) {
        fprintf(stderr, "[TLS] Failed to import private key from %s\n", key_path);
        if (key_items) CFRelease(key_items);
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        return -1;
    }
    fprintf(stderr, "[TLS] Imported %d key item(s) from %s\n",
            (int)CFArrayGetCount(key_items), key_path);
    CFRelease(key_items);

    /* Import certificate into the temp keychain */
    CFArrayRef cert_items = import_pem_file(cert_path, kSecItemTypeCertificate,
                                             temp_keychain);
    if (!cert_items || CFArrayGetCount(cert_items) == 0) {
        fprintf(stderr, "[TLS] Failed to import certificate from %s\n", cert_path);
        if (cert_items) CFRelease(cert_items);
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        return -1;
    }
    fprintf(stderr, "[TLS] Imported %d cert item(s) from %s\n",
            (int)CFArrayGetCount(cert_items), cert_path);

    /* Find the identity (cert+key pair) in the keychain.
     * SecIdentitySearchCreate + SecIdentitySearchCopyNext — available on Tiger.
     * These were deprecated in 10.7, but are the current API on 10.4. */
    SecIdentitySearchRef search = NULL;
    status = SecIdentitySearchCreate(temp_keychain, 0 /* any usage */, &search);
    if (status != noErr || !search) {
        fprintf(stderr, "[TLS] SecIdentitySearchCreate failed: %d\n", (int)status);
        CFRelease(cert_items);
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        return -1;
    }

    SecIdentityRef identity = NULL;
    status = SecIdentitySearchCopyNext(search, &identity);
    CFRelease(search);

    if (status != noErr || !identity) {
        fprintf(stderr, "[TLS] Failed to create identity from cert+key: %d\n",
                (int)status);
        CFRelease(cert_items);
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        return -1;
    }

    /* Build the identity+certs array for SSLSetCertificate.
     * First element must be the SecIdentityRef, followed by any
     * intermediate certificates from the cert file. */
    CFMutableArrayRef result = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(result, identity);

    /* Add any additional certificates from the cert file as chain certs */
    CFIndex cert_count = CFArrayGetCount(cert_items);
    CFIndex ci;
    for (ci = 1; ci < cert_count; ci++) {
        CFArrayAppendValue(result, CFArrayGetValueAtIndex(cert_items, ci));
    }

    *out_identity_certs = result;

    /* Cleanup — keep the keychain alive (identity references it).
     * The keychain file is cleaned up in hl_tls_server_ctx_free(). */
    CFRelease(identity);
    CFRelease(cert_items);
    CFRelease(temp_keychain);

    return 0;
}

/* --- Server context --- */

int hl_tls_server_ctx_init(hl_tls_server_ctx_t *ctx,
                            const char *cert_path,
                            const char *key_path)
{
    memset(ctx, 0, sizeof(*ctx));

    if (!cert_path || !key_path || cert_path[0] == '\0' || key_path[0] == '\0') {
        return -1;
    }

    if (load_identity_from_pem(cert_path, key_path,
                                &ctx->identity_certs) < 0) {
        return -1;
    }

    ctx->enabled = 1;
    fprintf(stderr, "[TLS] Certificate loaded successfully\n");
    return 0;
}

void hl_tls_server_ctx_free(hl_tls_server_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->identity_certs) {
        CFRelease(ctx->identity_certs);
        ctx->identity_certs = NULL;
    }

    /* Clean up temporary keychain file */
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/lemoniscate-tls-%d.keychain",
             getpid());
    unlink(temp_path);

    ctx->enabled = 0;
}

/* --- TLS accept --- */

hl_tls_conn_t *hl_tls_accept(hl_tls_server_ctx_t *ctx, int fd)
{
    if (!ctx || !ctx->enabled) {
        close(fd);
        return NULL;
    }

    /* SSLNewContext — Tiger API (SSLCreateContext is 10.8+).
     * First param: true = server side. */
    SSLContextRef ssl = NULL;
    OSStatus status = SSLNewContext(true, &ssl);
    if (status != noErr || !ssl) {
        close(fd);
        return NULL;
    }

    /* Allocate conn first so we can use &conn->fd as the connection ref */
    hl_tls_conn_t *conn = (hl_tls_conn_t *)calloc(1, sizeof(hl_tls_conn_t));
    if (!conn) {
        SSLDisposeContext(ssl);
        close(fd);
        return NULL;
    }
    conn->fd = fd;
    conn->ssl_ctx = ssl;

    /* Set I/O callbacks — pass pointer to conn->fd as the connection ref */
    status = SSLSetIOFuncs(ssl, tls_read_func, tls_write_func);
    if (status != noErr) goto fail;

    status = SSLSetConnection(ssl, &conn->fd);
    if (status != noErr) goto fail;

    /* Enable all supported protocol versions (SSL3 + TLS1 on Tiger).
     * Without this, SecureTransport may restrict to a subset that
     * doesn't match what the client offers. */
    {
        SSLProtocol proto;
        /* Allow SSL 3.0 through TLS 1.0 — the full range Tiger supports */
        status = SSLSetProtocolVersionEnabled(ssl, kSSLProtocolAll, true);
        if (status != noErr) {
            fprintf(stderr, "[TLS] SSLSetProtocolVersionEnabled failed: %d\n", (int)status);
        }
    }

    /* Allow all available cipher suites */
    {
        size_t numCiphers = 0;
        SSLGetNumberEnabledCiphers(ssl, &numCiphers);
        fprintf(stderr, "[TLS] %zu cipher suites enabled\n", numCiphers);
    }

    /* Set certificate */
    status = SSLSetCertificate(ssl, ctx->identity_certs);
    if (status != noErr) {
        fprintf(stderr, "[TLS] SSLSetCertificate failed: %d\n", (int)status);
        goto fail;
    }
    fprintf(stderr, "[TLS] SSLSetCertificate OK, starting handshake...\n");

    /* Perform TLS handshake — use select() to avoid busy-spinning
     * when the underlying socket would block. Timeout after 10s. */
    {
        int attempts = 0;
        do {
            status = SSLHandshake(ssl);
            if (status == errSSLWouldBlock) {
                fd_set fds;
                struct timeval tv;
                FD_ZERO(&fds);
                FD_SET(fd, &fds);
                tv.tv_sec = 10;
                tv.tv_usec = 0;
                if (select(fd + 1, &fds, &fds, NULL, &tv) <= 0) {
                    status = errSecIO; /* timeout */
                    break;
                }
                if (++attempts > 1000) {
                    status = errSecIO; /* safety limit */
                    break;
                }
            }
        } while (status == errSSLWouldBlock);
    }

    if (status != noErr) {
        fprintf(stderr, "[TLS] Handshake failed: %d\n", (int)status);
        goto fail;
    }

    return conn;

fail:
    SSLDisposeContext(ssl);
    close(fd);
    free(conn);
    return NULL;
}

/* --- Plain connection wrapper --- */

hl_tls_conn_t *hl_conn_wrap_plain(int fd)
{
    hl_tls_conn_t *conn = (hl_tls_conn_t *)calloc(1, sizeof(hl_tls_conn_t));
    if (!conn) return NULL;
    conn->fd = fd;
    conn->ssl_ctx = NULL;
    return conn;
}

/* --- Unified I/O --- */

ssize_t hl_conn_read(hl_tls_conn_t *conn, uint8_t *buf, size_t len)
{
    if (!conn) return -1;

    if (conn->ssl_ctx) {
        size_t processed = 0;
        OSStatus status = SSLRead(conn->ssl_ctx, buf, len, &processed);
        if (processed > 0) {
            return (ssize_t)processed;
        }
        if (status == errSSLWouldBlock) {
            /* No complete TLS record yet — tell caller to retry later.
             * Set EAGAIN so process_client_data treats it like EINTR. */
            errno = EAGAIN;
            return -1;
        }
        if (status == noErr) {
            return 0; /* EOF — clean close */
        }
        if (status == errSSLClosedGraceful || status == errSSLClosedNoNotify) {
            return 0; /* EOF */
        }
        return -1;
    }

    ssize_t r;
    do {
        r = read(conn->fd, buf, len);
    } while (r < 0 && errno == EINTR);
    return r;
}

int hl_conn_read_full(hl_tls_conn_t *conn, uint8_t *buf, size_t n)
{
    if (!conn) return -1;

    size_t total = 0;
    while (total < n) {
        if (conn->ssl_ctx) {
            size_t processed = 0;
            OSStatus status = SSLRead(conn->ssl_ctx, buf + total,
                                       n - total, &processed);
            if (processed > 0) {
                total += processed;
                continue;
            }
            if (status == errSSLWouldBlock) {
                /* Wait for socket readability instead of spinning */
                fd_set fds;
                struct timeval tv;
                FD_ZERO(&fds);
                FD_SET(conn->fd, &fds);
                tv.tv_sec = 30;
                tv.tv_usec = 0;
                if (select(conn->fd + 1, &fds, NULL, NULL, &tv) <= 0)
                    return -1; /* timeout or error */
                continue;
            }
            return -1; /* error or EOF */
        } else {
            ssize_t r = read(conn->fd, buf + total, n - total);
            if (r < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (r == 0) return -1; /* EOF */
            total += (size_t)r;
        }
    }
    return 0;
}

int hl_conn_write_all(hl_tls_conn_t *conn, const uint8_t *buf, size_t n)
{
    if (!conn) return -1;

    size_t total = 0;
    while (total < n) {
        if (conn->ssl_ctx) {
            size_t processed = 0;
            OSStatus status = SSLWrite(conn->ssl_ctx, buf + total,
                                        n - total, &processed);
            if (processed > 0) {
                total += processed;
                continue;
            }
            if (status == errSSLWouldBlock) {
                /* Wait for socket writability instead of spinning */
                fd_set fds;
                struct timeval tv;
                FD_ZERO(&fds);
                FD_SET(conn->fd, &fds);
                tv.tv_sec = 30;
                tv.tv_usec = 0;
                if (select(conn->fd + 1, NULL, &fds, NULL, &tv) <= 0)
                    return -1; /* timeout or error */
                continue;
            }
            return -1;
        } else {
            ssize_t w = write(conn->fd, buf + total, n - total);
            if (w < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (w == 0) return -1;
            total += (size_t)w;
        }
    }
    return 0;
}

void hl_conn_close(hl_tls_conn_t *conn)
{
    if (!conn) return;
    if (conn->ssl_ctx) {
        SSLClose(conn->ssl_ctx);
        SSLDisposeContext(conn->ssl_ctx);
        conn->ssl_ctx = NULL;
    }
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    free(conn);
}

void hl_conn_free(hl_tls_conn_t *conn)
{
    if (!conn) return;
    if (conn->ssl_ctx) {
        SSLClose(conn->ssl_ctx);
        SSLDisposeContext(conn->ssl_ctx);
        conn->ssl_ctx = NULL;
    }
    /* Do NOT close the fd — caller manages it */
    free(conn);
}

#else /* !__APPLE__ */

/* Stub implementations for non-Apple platforms */

int hl_tls_server_ctx_init(hl_tls_server_ctx_t *ctx,
                            const char *cert_path,
                            const char *key_path)
{
    (void)ctx; (void)cert_path; (void)key_path;
    fprintf(stderr, "[TLS] Not supported on this platform\n");
    return -1;
}

void hl_tls_server_ctx_free(hl_tls_server_ctx_t *ctx) { (void)ctx; }

hl_tls_conn_t *hl_tls_accept(hl_tls_server_ctx_t *ctx, int fd)
{
    (void)ctx; close(fd); return NULL;
}

hl_tls_conn_t *hl_conn_wrap_plain(int fd)
{
    hl_tls_conn_t *conn = (hl_tls_conn_t *)calloc(1, sizeof(hl_tls_conn_t));
    if (!conn) return NULL;
    conn->fd = fd;
    conn->ssl_ctx = NULL;
    return conn;
}

ssize_t hl_conn_read(hl_tls_conn_t *conn, uint8_t *buf, size_t len)
{
    if (!conn) return -1;
    ssize_t r;
    do { r = read(conn->fd, buf, len); } while (r < 0 && errno == EINTR);
    return r;
}

int hl_conn_read_full(hl_tls_conn_t *conn, uint8_t *buf, size_t n)
{
    if (!conn) return -1;
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(conn->fd, buf + total, n - total);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return -1;
        total += (size_t)r;
    }
    return 0;
}

int hl_conn_write_all(hl_tls_conn_t *conn, const uint8_t *buf, size_t n)
{
    if (!conn) return -1;
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(conn->fd, buf + total, n - total);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        total += (size_t)w;
    }
    return 0;
}

void hl_conn_close(hl_tls_conn_t *conn)
{
    if (!conn) return;
    if (conn->fd >= 0) close(conn->fd);
    free(conn);
}

void hl_conn_free(hl_tls_conn_t *conn)
{
    if (!conn) return;
    free(conn);
}

#endif /* __APPLE__ */
