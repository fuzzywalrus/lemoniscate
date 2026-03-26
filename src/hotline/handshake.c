/*
 * handshake.c - Hotline protocol handshake implementation
 *
 * Maps to: hotline/handshake.go, hotline/client.go (Client.Handshake)
 */

#include "hotline/handshake.h"
#include "hotline/tls.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

/* Read exactly n bytes from fd. Returns 0 on success, -1 on error. */
static int read_full(int fd, uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1; /* EOF */
        total += (size_t)r;
    }
    return 0;
}

/* Write exactly n bytes to fd. Returns 0 on success, -1 on error. */
static int write_full(int fd, const uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        total += (size_t)w;
    }
    return 0;
}

int hl_handshake_parse(hl_handshake_t *h, const uint8_t *buf, size_t buf_len)
{
    if (buf_len != HL_HANDSHAKE_SIZE) return -1;

    memcpy(h->protocol,     buf,     4);
    memcpy(h->sub_protocol, buf + 4, 4);
    memcpy(h->version,      buf + 8, 2);
    memcpy(h->sub_version,  buf + 10, 2);

    return 0;
}

int hl_handshake_valid(const hl_handshake_t *h)
{
    return memcmp(h->protocol, HL_PROTO_TRTP, 4) == 0 &&
           memcmp(h->sub_protocol, HL_PROTO_HOTL, 4) == 0;
}

int hl_perform_handshake_server(int fd)
{
    /* Maps to: Go performHandshake() in handshake.go */
    uint8_t buf[HL_HANDSHAKE_SIZE];

    if (read_full(fd, buf, HL_HANDSHAKE_SIZE) < 0)
        return -1;

    hl_handshake_t h;
    if (hl_handshake_parse(&h, buf, HL_HANDSHAKE_SIZE) < 0)
        return -1;

    if (!hl_handshake_valid(&h))
        return -1;

    if (write_full(fd, HL_SERVER_HANDSHAKE_RESPONSE, 8) < 0)
        return -1;

    return 0;
}

int hl_perform_handshake_server_conn(hl_tls_conn_t *conn)
{
    /* TLS-aware server handshake — same protocol, uses conn wrapper I/O */
    uint8_t buf[HL_HANDSHAKE_SIZE];

    if (hl_conn_read_full(conn, buf, HL_HANDSHAKE_SIZE) < 0)
        return -1;

    hl_handshake_t h;
    if (hl_handshake_parse(&h, buf, HL_HANDSHAKE_SIZE) < 0)
        return -1;

    if (!hl_handshake_valid(&h))
        return -1;

    if (hl_conn_write_all(conn, HL_SERVER_HANDSHAKE_RESPONSE, 8) < 0)
        return -1;

    return 0;
}

int hl_perform_handshake_client(int fd)
{
    /* Maps to: Go Client.Handshake() in client.go */
    if (write_full(fd, HL_CLIENT_HANDSHAKE, HL_HANDSHAKE_SIZE) < 0)
        return -1;

    uint8_t reply[8];
    if (read_full(fd, reply, 8) < 0)
        return -1;

    if (memcmp(reply, HL_SERVER_HANDSHAKE_RESPONSE, 8) != 0)
        return -1;

    return 0;
}
