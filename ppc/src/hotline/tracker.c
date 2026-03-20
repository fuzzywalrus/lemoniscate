/*
 * tracker.c - Tracker server registration via UDP
 *
 * Maps to: hotline/tracker.go
 *
 * Wire format:
 *   Magic(2: 0x0001) + Port(2) + UserCount(2) + TLSPort(2) + PassID(4)
 *   + NameLen(1) + Name + DescLen(1) + Desc + PassLen(1) + Password
 */

#include "hotline/tracker.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int hl_tracker_register(const char *tracker_addr,
                        uint16_t server_port,
                        uint16_t user_count,
                        const uint8_t pass_id[4],
                        const char *name,
                        const char *description,
                        const char *password)
{
    /* Parse host:port */
    char host[256];
    int port = 5499; /* Default tracker port (standard Hotline tracker) */
    strncpy(host, tracker_addr, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    /* Resolve host */
    struct hostent *he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "[TRACKER] DNS resolution failed for '%s'\n", host);
        return -1;
    }

    /* Create UDP socket */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        fprintf(stderr, "[TRACKER] Failed to create UDP socket for '%s:%d'\n",
                host, port);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    /* Build registration packet */
    uint8_t buf[1024];
    size_t pos = 0;

    /* Magic: 0x0001 */
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* Port (BE uint16) */
    hl_write_u16(buf + pos, server_port);
    pos += 2;

    /* UserCount (BE uint16) */
    hl_write_u16(buf + pos, user_count);
    pos += 2;

    /* TLS Port (BE uint16) — 0 for no TLS */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* PassID (4 bytes) */
    memcpy(buf + pos, pass_id, 4);
    pos += 4;

    /* Name: length-prefixed string */
    size_t name_len = strlen(name);
    if (name_len > 255) name_len = 255;
    buf[pos++] = (uint8_t)name_len;
    memcpy(buf + pos, name, name_len);
    pos += name_len;

    /* Description: length-prefixed string */
    size_t desc_len = strlen(description);
    if (desc_len > 255) desc_len = 255;
    buf[pos++] = (uint8_t)desc_len;
    memcpy(buf + pos, description, desc_len);
    pos += desc_len;

    /* Password: length-prefixed string */
    size_t pass_len = password ? strlen(password) : 0;
    if (pass_len > 255) pass_len = 255;
    buf[pos++] = (uint8_t)pass_len;
    if (pass_len > 0) {
        memcpy(buf + pos, password, pass_len);
        pos += pass_len;
    }

    /* Send via UDP */
    ssize_t sent = sendto(fd, buf, pos, 0,
                          (struct sockaddr *)&addr, sizeof(addr));
    close(fd);

    if (sent <= 0) {
        fprintf(stderr, "[TRACKER] %s:%d -> send failed (%zd/%zu bytes)\n",
                host, port, sent, pos);
        return -1;
    }

    fprintf(stderr, "[TRACKER] %s:%d -> sent %zd/%zu bytes (name=\"%s\" port=%d users=%d)\n",
            host, port, sent, pos, name, server_port, user_count);
    fflush(stderr);

    return 0;
}

int hl_tracker_register_all(const char trackers[][256], int tracker_count,
                            uint16_t server_port,
                            uint16_t user_count,
                            const uint8_t pass_id[4],
                            const char *name,
                            const char *description)
{
    int i;
    int success_count = 0;
    for (i = 0; i < tracker_count; i++) {
        /* Parse "host:port" or "host:port:password" */
        char entry[256];
        strncpy(entry, trackers[i], sizeof(entry) - 1);
        entry[sizeof(entry) - 1] = '\0';

        char *password = "";
        /* Find third colon for password */
        char *first_colon = strchr(entry, ':');
        if (first_colon) {
            char *second_colon = strchr(first_colon + 1, ':');
            if (second_colon) {
                *second_colon = '\0';
                password = second_colon + 1;
            }
        }

        if (hl_tracker_register(entry, server_port, user_count,
                               pass_id, name, description, password) == 0) {
            success_count++;
        }
    }
    return success_count;
}
