/*
 * event_epoll.c - epoll + timerfd backend for platform event loop (Linux)
 *
 * This file is only compiled on Linux.
 */

#include "hotline/platform/platform_event.h"

#ifdef __linux__

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Track fd-to-udata mapping for returning udata in events.
 * Using a simple linear array — Hotline servers typically have
 * < 200 concurrent connections. */
#define MAX_FDS 4096

typedef struct {
    int   timer_id;
    int   timerfd;
} hl_timer_entry_t;

#define MAX_TIMERS 16

struct hl_event_loop {
    int              epfd;
    void            *udata[MAX_FDS];     /* fd -> udata mapping */
    hl_timer_entry_t timers[MAX_TIMERS];
    int              timer_count;
};

hl_event_loop_t *hl_event_loop_new(void)
{
    hl_event_loop_t *loop = calloc(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epfd < 0) {
        free(loop);
        return NULL;
    }
    memset(loop->udata, 0, sizeof(loop->udata));
    loop->timer_count = 0;
    return loop;
}

void hl_event_loop_free(hl_event_loop_t *loop)
{
    if (!loop) return;
    int i;
    for (i = 0; i < loop->timer_count; i++) {
        if (loop->timers[i].timerfd >= 0)
            close(loop->timers[i].timerfd);
    }
    if (loop->epfd >= 0) close(loop->epfd);
    free(loop);
}

int hl_event_add_fd(hl_event_loop_t *loop, int fd, void *udata)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;

    loop->udata[fd] = udata;

    return epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) < 0 ? -1 : 0;
}

int hl_event_remove_fd(hl_event_loop_t *loop, int fd)
{
    if (fd < 0 || fd >= MAX_FDS) return -1;

    loop->udata[fd] = NULL;
    /* EPOLL_CTL_DEL: ev can be NULL on Linux 2.6.9+ */
    epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL);
    return 0;
}

int hl_event_add_timer(hl_event_loop_t *loop, int timer_id,
                       uint64_t interval_ms)
{
    if (loop->timer_count >= MAX_TIMERS) return -1;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0) return -1;

    struct itimerspec its;
    its.it_value.tv_sec = (time_t)(interval_ms / 1000);
    its.it_value.tv_nsec = (long)((interval_ms % 1000) * 1000000);
    its.it_interval = its.it_value; /* repeating */

    if (timerfd_settime(tfd, 0, &its, NULL) < 0) {
        close(tfd);
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = tfd;

    if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, tfd, &ev) < 0) {
        close(tfd);
        return -1;
    }

    hl_timer_entry_t *entry = &loop->timers[loop->timer_count++];
    entry->timer_id = timer_id;
    entry->timerfd = tfd;

    return 0;
}

int hl_event_remove_timer(hl_event_loop_t *loop, int timer_id)
{
    int i;
    for (i = 0; i < loop->timer_count; i++) {
        if (loop->timers[i].timer_id == timer_id) {
            epoll_ctl(loop->epfd, EPOLL_CTL_DEL, loop->timers[i].timerfd, NULL);
            close(loop->timers[i].timerfd);
            /* Shift remaining entries */
            loop->timers[i] = loop->timers[loop->timer_count - 1];
            loop->timer_count--;
            return 0;
        }
    }
    return -1;
}

/* Check if an fd is one of our timer fds */
static int find_timer(hl_event_loop_t *loop, int fd)
{
    int i;
    for (i = 0; i < loop->timer_count; i++) {
        if (loop->timers[i].timerfd == fd)
            return i;
    }
    return -1;
}

int hl_event_poll(hl_event_loop_t *loop, hl_event_t *events,
                  int max_events, int timeout_ms)
{
    struct epoll_event eevents[64];
    if (max_events > 64) max_events = 64;

    int nev = epoll_wait(loop->epfd, eevents, max_events, timeout_ms);
    if (nev < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    int i;
    for (i = 0; i < nev; i++) {
        struct epoll_event *eev = &eevents[i];
        int fd = eev->data.fd;

        int timer_idx = find_timer(loop, fd);
        if (timer_idx >= 0) {
            /* Timer event — drain the timerfd */
            uint64_t expirations;
            (void)read(fd, &expirations, sizeof(expirations));
            events[i].type = HL_EVENT_TIMER;
            events[i].fd = loop->timers[timer_idx].timer_id;
            events[i].udata = NULL;
        } else if (eev->events & (EPOLLHUP | EPOLLERR)) {
            events[i].type = HL_EVENT_EOF;
            events[i].fd = fd;
            events[i].udata = (fd >= 0 && fd < MAX_FDS) ? loop->udata[fd] : NULL;
        } else {
            events[i].type = HL_EVENT_READ;
            events[i].fd = fd;
            events[i].udata = (fd >= 0 && fd < MAX_FDS) ? loop->udata[fd] : NULL;
        }
    }

    return nev;
}

#endif /* __linux__ */
