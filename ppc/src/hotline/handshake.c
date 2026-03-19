/*
 * handshake.c - Hotline protocol handshake implementation
 *
 * Maps to: hotline/handshake.go, hotline/client.go (Client.Handshake)
 */

#include "hotline/handshake.h"
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

    if (read_full(fd, buf, HL_HANDSHAKE_SIZE) < 0) {
        fprintf(stderr, "[HANDSHAKE] read failed (timeout or EOF)\n");
        fflush(stderr);
        return -1;
    }

    fprintf(stderr, "[HANDSHAKE] recv %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
            buf[0],buf[1],buf[2],buf[3], buf[4],buf[5],buf[6],buf[7],
            buf[8],buf[9],buf[10],buf[11]);
    fflush(stderr);

    hl_handshake_t h;
    if (hl_handshake_parse(&h, buf, HL_HANDSHAKE_SIZE) < 0)
        return -1;

    if (!hl_handshake_valid(&h)) {
        fprintf(stderr, "[HANDSHAKE] invalid: proto=%c%c%c%c sub=%c%c%c%c\n",
                buf[0],buf[1],buf[2],buf[3], buf[4],buf[5],buf[6],buf[7]);
        fflush(stderr);
        return -1;
    }

    if (write_full(fd, HL_SERVER_HANDSHAKE_RESPONSE, 8) < 0)
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
