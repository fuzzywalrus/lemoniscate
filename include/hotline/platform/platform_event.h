/*
 * platform_event.h - Platform-abstracted event loop API
 *
 * macOS: kqueue backend (event_kqueue.c)
 * Linux: epoll + timerfd backend (event_epoll.c)
 */

#ifndef HOTLINE_PLATFORM_EVENT_H
#define HOTLINE_PLATFORM_EVENT_H

#include <stdint.h>

/* Event types returned by hl_event_poll */
typedef enum {
    HL_EVENT_READ,     /* fd has data available */
    HL_EVENT_TIMER,    /* timer fired */
    HL_EVENT_EOF       /* fd closed / peer disconnected */
} hl_event_type_t;

/* Opaque event returned from poll */
typedef struct {
    hl_event_type_t type;
    int             fd;        /* source fd (for READ/EOF events) */
    void           *udata;     /* user data associated with the fd */
} hl_event_t;

/* Opaque event loop handle */
typedef struct hl_event_loop hl_event_loop_t;

/*
 * hl_event_loop_new - Create a new event loop.
 * Returns NULL on failure.
 */
hl_event_loop_t *hl_event_loop_new(void);

/*
 * hl_event_loop_free - Destroy the event loop and release resources.
 */
void hl_event_loop_free(hl_event_loop_t *loop);

/*
 * hl_event_add_fd - Register a file descriptor for read events.
 * udata is returned in hl_event_t when the fd fires.
 * Returns 0 on success, -1 on error.
 */
int hl_event_add_fd(hl_event_loop_t *loop, int fd, void *udata);

/*
 * hl_event_remove_fd - Unregister a file descriptor.
 * Returns 0 on success, -1 on error.
 */
int hl_event_remove_fd(hl_event_loop_t *loop, int fd);

/*
 * hl_event_add_timer - Register a periodic timer.
 * interval_ms is the timer period in milliseconds.
 * timer_id is a caller-chosen identifier (used to remove later).
 * Returns 0 on success, -1 on error.
 */
int hl_event_add_timer(hl_event_loop_t *loop, int timer_id,
                       uint64_t interval_ms);

/*
 * hl_event_remove_timer - Remove a periodic timer.
 * Returns 0 on success, -1 on error.
 */
int hl_event_remove_timer(hl_event_loop_t *loop, int timer_id);

/*
 * hl_event_poll - Wait for events, up to max_events.
 * timeout_ms: maximum wait time in milliseconds (0 = no wait, -1 = indefinite).
 * events: caller-provided array of at least max_events entries.
 * Returns number of events available (0 on timeout), -1 on error.
 * On EINTR, returns 0 (caller should retry).
 */
int hl_event_poll(hl_event_loop_t *loop, hl_event_t *events,
                  int max_events, int timeout_ms);

#endif /* HOTLINE_PLATFORM_EVENT_H */
