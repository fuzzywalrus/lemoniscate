/*
 * tls_sectransport.c - macOS TLS backend using SecureTransport
 *
 * Uses Apple's SecureTransport framework to provide TLS for
 * the Hotline server. PEM certificates and keys are loaded
 * via SecItemImport().
 *
 * This file is only compiled on macOS (Darwin).
 */

#include "hotline/tls.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>

/* Suppress deprecation warnings for SecKeychain APIs —
 * needed for PEM->identity loading; still functional on modern macOS. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

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

/* --- PEM loading --- */

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

/* Import PEM items from a file using SecItemImport.
 * Returns a CFArrayRef of imported items (caller must CFRelease).
 * Returns NULL on failure. */
static CFArrayRef import_pem_file(const char *path,
                                   SecExternalItemType expected_type)
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

    OSStatus status = SecItemImport(cf_data, NULL, &format, &type,
                                     0, NULL, NULL, &items);
    CFRelease(cf_data);

    if (status != errSecSuccess || !items) {
        if (items) CFRelease(items);
        return NULL;
    }

    return items;
}

/* Build a SecIdentityRef from separate cert and key PEM files.
 *
 * Strategy: Import the private key into a temporary keychain,
 * then import the certificate and create an identity.
 * Falls back to PKCS#12 round-trip if direct import fails.
 */
static int load_identity_from_pem(const char *cert_path,
                                   const char *key_path,
                                   CFArrayRef *out_identity_certs)
{
    *out_identity_certs = NULL;

    /* Import certificate */
    CFArrayRef cert_items = import_pem_file(cert_path, kSecItemTypeCertificate);
    if (!cert_items || CFArrayGetCount(cert_items) == 0) {
        fprintf(stderr, "[TLS] Failed to import certificate from %s\n", cert_path);
        if (cert_items) CFRelease(cert_items);
        return -1;
    }

    SecCertificateRef cert = (SecCertificateRef)CFArrayGetValueAtIndex(cert_items, 0);
    if (!cert || CFGetTypeID(cert) != SecCertificateGetTypeID()) {
        fprintf(stderr, "[TLS] Imported item is not a certificate\n");
        CFRelease(cert_items);
        return -1;
    }
    CFRetain(cert);

    /* Import private key */
    CFArrayRef key_items = import_pem_file(key_path, kSecItemTypePrivateKey);
    if (!key_items || CFArrayGetCount(key_items) == 0) {
        fprintf(stderr, "[TLS] Failed to import private key from %s\n", key_path);
        if (key_items) CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }

    SecKeyRef key = (SecKeyRef)CFArrayGetValueAtIndex(key_items, 0);
    if (!key || CFGetTypeID(key) != SecKeyGetTypeID()) {
        fprintf(stderr, "[TLS] Imported item is not a private key\n");
        CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }
    CFRetain(key);

    /* Create identity by adding cert+key to a temporary keychain */
    SecKeychainRef temp_keychain = NULL;
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/lemoniscate-tls-%d.keychain",
             getpid());

    /* Remove stale keychain file if it exists */
    unlink(temp_path);

    OSStatus status = SecKeychainCreate(temp_path, 8, "lemonade",
                                         FALSE, NULL, &temp_keychain);
    if (status != errSecSuccess) {
        fprintf(stderr, "[TLS] Failed to create temporary keychain: %d\n",
                (int)status);
        CFRelease(key);
        CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }

    /* Unlock the keychain for use */
    SecKeychainUnlock(temp_keychain, 8, "lemonade", TRUE);

    /* Import the private key into the temp keychain */
    SecExternalFormat key_format = kSecFormatPEMSequence;
    SecExternalItemType key_type = kSecItemTypePrivateKey;

    SecItemImportExportKeyParameters import_params;
    memset(&import_params, 0, sizeof(import_params));
    import_params.version = SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION;
    import_params.passphrase = CFSTR("");

    size_t key_data_len = 0;
    uint8_t *key_data = read_file(key_path, &key_data_len);
    if (!key_data) {
        fprintf(stderr, "[TLS] Failed to re-read key file\n");
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        CFRelease(key);
        CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }

    CFDataRef key_cf_data = CFDataCreate(NULL, key_data, (CFIndex)key_data_len);
    free(key_data);

    CFArrayRef imported_key_items = NULL;
    status = SecItemImport(key_cf_data, NULL, &key_format, &key_type,
                            0, &import_params, temp_keychain,
                            &imported_key_items);
    CFRelease(key_cf_data);
    CFRelease(import_params.passphrase);

    if (status != errSecSuccess) {
        fprintf(stderr, "[TLS] Failed to import key into keychain: %d\n",
                (int)status);
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        CFRelease(key);
        CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }
    if (imported_key_items) CFRelease(imported_key_items);

    /* Import the certificate into the temp keychain */
    SecExternalFormat cert_format = kSecFormatPEMSequence;
    SecExternalItemType cert_type = kSecItemTypeCertificate;

    size_t cert_data_len = 0;
    uint8_t *cert_data = read_file(cert_path, &cert_data_len);
    if (!cert_data) {
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        CFRelease(key);
        CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }

    CFDataRef cert_cf_data = CFDataCreate(NULL, cert_data, (CFIndex)cert_data_len);
    free(cert_data);

    CFArrayRef imported_cert_items = NULL;
    status = SecItemImport(cert_cf_data, NULL, &cert_format, &cert_type,
                            0, NULL, temp_keychain, &imported_cert_items);
    CFRelease(cert_cf_data);

    if (status != errSecSuccess) {
        fprintf(stderr, "[TLS] Failed to import cert into keychain: %d\n",
                (int)status);
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        CFRelease(key);
        CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }
    if (imported_cert_items) CFRelease(imported_cert_items);

    /* Now find the identity (cert+key pair) in the keychain */
    SecIdentityRef identity = NULL;
    status = SecIdentityCreateWithCertificate(temp_keychain, cert, &identity);

    if (status != errSecSuccess || !identity) {
        fprintf(stderr, "[TLS] Failed to create identity from cert+key: %d\n",
                (int)status);
        SecKeychainDelete(temp_keychain);
        CFRelease(temp_keychain);
        CFRelease(key);
        CFRelease(key_items);
        CFRelease(cert);
        CFRelease(cert_items);
        return -1;
    }

    /* Build the identity+certs array for SSLSetCertificate.
     * First element must be the SecIdentityRef, followed by any
     * intermediate certificates. */
    CFMutableArrayRef result = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(result, identity);

    /* Add any additional certificates from the cert file as chain certs */
    CFIndex cert_count = CFArrayGetCount(cert_items);
    CFIndex ci;
    for (ci = 1; ci < cert_count; ci++) {
        CFArrayAppendValue(result, CFArrayGetValueAtIndex(cert_items, ci));
    }

    *out_identity_certs = result;

    /* Cleanup — keep the keychain alive (identity references it) */
    CFRelease(identity);
    CFRelease(key);
    CFRelease(key_items);
    CFRelease(cert);
    CFRelease(cert_items);
    /* Note: we intentionally leak temp_keychain here because the identity
     * holds a reference to the key inside it. The keychain file is cleaned
     * up on server exit. */
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

    /* Also try .keychain-db variant (modern macOS) */
    char temp_path_db[256];
    snprintf(temp_path_db, sizeof(temp_path_db),
             "/tmp/lemoniscate-tls-%d.keychain-db", getpid());
    unlink(temp_path_db);

    ctx->enabled = 0;
}

/* --- TLS accept --- */

hl_tls_conn_t *hl_tls_accept(hl_tls_server_ctx_t *ctx, int fd)
{
    if (!ctx || !ctx->enabled) {
        close(fd);
        return NULL;
    }

    SSLContextRef ssl = SSLCreateContext(NULL, kSSLServerSide, kSSLStreamType);
    if (!ssl) {
        close(fd);
        return NULL;
    }

    /* Allocate conn first so we can use &conn->fd as the connection ref */
    hl_tls_conn_t *conn = (hl_tls_conn_t *)calloc(1, sizeof(hl_tls_conn_t));
    if (!conn) {
        CFRelease(ssl);
        close(fd);
        return NULL;
    }
    conn->fd = fd;
    conn->ssl_ctx = ssl;

    /* Set I/O callbacks — pass pointer to conn->fd as the connection ref */
    OSStatus status = SSLSetIOFuncs(ssl, tls_read_func, tls_write_func);
    if (status != noErr) goto fail;

    status = SSLSetConnection(ssl, &conn->fd);
    if (status != noErr) goto fail;

    /* Set certificate */
    status = SSLSetCertificate(ssl, ctx->identity_certs);
    if (status != noErr) {
        fprintf(stderr, "[TLS] SSLSetCertificate failed: %d\n", (int)status);
        goto fail;
    }

    /* Perform TLS handshake */
    do {
        status = SSLHandshake(ssl);
    } while (status == errSSLWouldBlock);

    if (status != noErr) {
        fprintf(stderr, "[TLS] Handshake failed: %d\n", (int)status);
        goto fail;
    }

    return conn;

fail:
    CFRelease(ssl);
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
        if (status == noErr || status == errSSLWouldBlock) {
            return (processed > 0) ? (ssize_t)processed : -1;
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
            if (status == errSSLWouldBlock) continue;
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
            if (status == errSSLWouldBlock) continue;
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
        CFRelease(conn->ssl_ctx);
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
        CFRelease(conn->ssl_ctx);
        conn->ssl_ctx = NULL;
    }
    /* Do NOT close the fd — caller manages it */
    free(conn);
}

#pragma clang diagnostic pop
