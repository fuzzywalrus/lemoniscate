/*
 * http_client.c - Minimal HTTP POST client for Mnemosyne sync
 *
 * Blocking HTTP/1.1 POST. No chunked transfer, no keepalive, no TLS.
 * Designed for small JSON payloads to the Mnemosyne sync API.
 */

#include "hotline/http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

/* --- URL parsing --- */

int hl_http_parse_url(const char *url, hl_parsed_url_t *out)
{
    memset(out, 0, sizeof(*out));
    out->port = 80;

    /* Skip "http://" prefix */
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;
    else if (strncmp(p, "https://", 8) == 0)
        return -1; /* HTTPS not supported */

    /* Extract host[:port] */
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t host_len;
    if (colon && (!slash || colon < slash)) {
        /* host:port */
        host_len = (size_t)(colon - p);
        if (host_len >= sizeof(out->host))
            return -1;
        memcpy(out->host, p, host_len);
        out->host[host_len] = '\0';
        out->port = atoi(colon + 1);
        if (out->port <= 0 || out->port > 65535)
            return -1;
    } else if (slash) {
        host_len = (size_t)(slash - p);
        if (host_len >= sizeof(out->host))
            return -1;
        memcpy(out->host, p, host_len);
        out->host[host_len] = '\0';
    } else {
        host_len = strlen(p);
        if (host_len >= sizeof(out->host))
            return -1;
        memcpy(out->host, p, host_len);
        out->host[host_len] = '\0';
    }

    /* Extract path */
    if (slash) {
        strncpy(out->path, slash, sizeof(out->path) - 1);
    } else {
        out->path[0] = '/';
        out->path[1] = '\0';
    }

    return 0;
}

int hl_http_url_with_api_key(const hl_parsed_url_t *url, const char *api_key,
                             char *out_buf, size_t out_size)
{
    /* Check if path already has query params */
    const char *sep = strchr(url->path, '?') ? "&" : "?";
    int n = snprintf(out_buf, out_size, "%s%sapi_key=%s", url->path, sep, api_key);
    if (n < 0 || (size_t)n >= out_size)
        return -1;
    return 0;
}

/* --- TCP connect with timeout --- */

static int tcp_connect(const char *ip, int port, int timeout_ms)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
        return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    /* Set non-blocking for connect timeout */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    if (rc < 0) {
        /* Wait for connect with timeout */
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0) {
            close(fd);
            return -1; /* timeout or error */
        }

        /* Check for connect error */
        int so_err = 0;
        socklen_t len = sizeof(so_err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len);
        if (so_err != 0) {
            close(fd);
            return -1;
        }
    }

    /* Restore blocking mode */
    fcntl(fd, F_SETFL, flags);

    return fd;
}

/* --- Set read timeout on socket --- */

static void set_read_timeout(int fd, int timeout_ms)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* --- Send all bytes --- */

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* --- Read HTTP status code from response --- */

static int read_status_code(int fd)
{
    /* Read enough to get the status line: "HTTP/1.1 200 OK\r\n..." */
    char buf[256];
    ssize_t total = 0;

    while (total < (ssize_t)sizeof(buf) - 1) {
        ssize_t n = recv(fd, buf + total, sizeof(buf) - 1 - (size_t)total, 0);
        if (n <= 0)
            break;
        total += n;
        buf[total] = '\0';
        /* Stop once we have the first line */
        if (strstr(buf, "\r\n"))
            break;
    }

    if (total <= 0)
        return -1;

    buf[total] = '\0';

    /* Parse "HTTP/1.x NNN" */
    if (strncmp(buf, "HTTP/1.", 7) != 0)
        return -1;

    const char *status_start = strchr(buf, ' ');
    if (!status_start)
        return -1;

    return atoi(status_start + 1);
}

/* --- HTTP POST core implementation --- */

static int do_http_post(const char *ip, const char *hostname, int port,
                        const char *path, const char *body, size_t body_len,
                        const char *content_type,
                        int connect_timeout_ms, int read_timeout_ms)
{
    int fd = tcp_connect(ip, port, connect_timeout_ms);
    if (fd < 0)
        return -1;

    set_read_timeout(fd, read_timeout_ms);

    /* Build HTTP request header */
    char header[2048];
    int hlen = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, hostname, port, content_type, body_len);

    if (hlen < 0 || (size_t)hlen >= sizeof(header)) {
        close(fd);
        return -1;
    }

    /* Send header + body */
    if (send_all(fd, header, (size_t)hlen) < 0) {
        close(fd);
        return -1;
    }
    if (body_len > 0 && send_all(fd, body, body_len) < 0) {
        close(fd);
        return -1;
    }

    /* Read response status */
    int status = read_status_code(fd);
    close(fd);
    return status;
}

/* --- Public API --- */

int hl_http_post(const char *host, int port, const char *path,
                 const char *body, size_t body_len,
                 const char *content_type,
                 int connect_timeout_ms, int read_timeout_ms)
{
    /* Resolve hostname to IP */
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, NULL, &hints, &res) != 0)
        return -1;

    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
    freeaddrinfo(res);

    return do_http_post(ip, host, port, path, body, body_len,
                        content_type, connect_timeout_ms, read_timeout_ms);
}

int hl_http_post_to_ip(const char *ip, const char *hostname, int port,
                       const char *path, const char *body, size_t body_len,
                       const char *content_type,
                       int connect_timeout_ms, int read_timeout_ms)
{
    return do_http_post(ip, hostname, port, path, body, body_len,
                        content_type, connect_timeout_ms, read_timeout_ms);
}
