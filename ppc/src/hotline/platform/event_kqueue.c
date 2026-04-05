/*
 * event_kqueue.c - kqueue backend for platform event loop (macOS/BSD)
 */

#include "hotline/platform/platform_event.h"
#include <sys/event.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

struct hl_event_loop {
    int kq;
};

hl_event_loop_t *hl_event_loop_new(void)
{
    hl_event_loop_t *loop = calloc(1, sizeof(*loop));
    if (!loop) return NULL;

    loop->kq = kqueue();
    if (loop->kq < 0) {
        free(loop);
        return NULL;
    }
    return loop;
}

void hl_event_loop_free(hl_event_loop_t *loop)
{
    if (!loop) return;
    if (loop->kq >= 0) close(loop->kq);
    free(loop);
}

int hl_event_add_fd(hl_event_loop_t *loop, int fd, void *udata)
{
    struct kevent change;
    EV_SET(&change, fd, EVFILT_READ, EV_ADD, 0, 0, udata);
    return kevent(loop->kq, &change, 1, NULL, 0, NULL) < 0 ? -1 : 0;
}

int hl_event_remove_fd(hl_event_loop_t *loop, int fd)
{
    struct kevent change;
    EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    /* Ignore errors — fd may already be closed */
    kevent(loop->kq, &change, 1, NULL, 0, NULL);
    return 0;
}

int hl_event_add_timer(hl_event_loop_t *loop, int timer_id,
                       uint64_t interval_ms)
{
    struct kevent change;
    EV_SET(&change, timer_id, EVFILT_TIMER, EV_ADD, 0,
           (intptr_t)interval_ms, NULL);
    return kevent(loop->kq, &change, 1, NULL, 0, NULL) < 0 ? -1 : 0;
}

int hl_event_remove_timer(hl_event_loop_t *loop, int timer_id)
{
    struct kevent change;
    EV_SET(&change, timer_id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
    kevent(loop->kq, &change, 1, NULL, 0, NULL);
    return 0;
}

int hl_event_poll(hl_event_loop_t *loop, hl_event_t *events,
                  int max_events, int timeout_ms)
{
    struct timespec ts;
    struct timespec *tsp = NULL;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
        tsp = &ts;
    }

    struct kevent kevents[64];
    if (max_events > 64) max_events = 64;

    int nev = kevent(loop->kq, NULL, 0, kevents, max_events, tsp);
    if (nev < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }

    int i;
    for (i = 0; i < nev; i++) {
        struct kevent *kev = &kevents[i];

        if (kev->filter == EVFILT_TIMER) {
            events[i].type = HL_EVENT_TIMER;
            events[i].fd = (int)kev->ident;
            events[i].udata = kev->udata;
        } else if (kev->flags & EV_EOF) {
            events[i].type = HL_EVENT_EOF;
            events[i].fd = (int)kev->ident;
            events[i].udata = kev->udata;
        } else {
            events[i].type = HL_EVENT_READ;
            events[i].fd = (int)kev->ident;
            events[i].udata = kev->udata;
        }
    }

    return nev;
}
