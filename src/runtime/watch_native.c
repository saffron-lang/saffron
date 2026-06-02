/*
 * Saffron Runtime: File/Directory Watcher Primitives
 * ==================================================
 *
 * This module provides cross-platform file system watching for the Saffron
 * native compiled runtime. All functions use the sf_ prefix and are designed
 * to be called from LLVM IR via `declare` directives.
 *
 * Non-blocking design:
 *   sf_watch_poll() accepts a timeout in milliseconds, allowing the caller
 *   to integrate with the cooperative scheduler. A timeout of 0 performs a
 *   non-blocking check; any positive value waits up to that duration.
 *
 * Platform support:
 *   - macOS (Darwin): uses kqueue + kevent with EVFILT_VNODE
 *   - Linux: uses inotify + poll()
 *
 * Watched events:
 *   - File content modified (write)
 *   - File deleted
 *   - File renamed/moved
 *   - File attributes changed
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __APPLE__

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

/*
 * sf_watch_init — Create a kqueue file descriptor for watching files.
 *
 * Allocates a new kqueue instance that can be used to monitor file system
 * events. The returned fd should be passed to subsequent watch operations.
 *
 * Returns: kqueue fd on success, -1 on error.
 */
int64_t sf_watch_init(void) {
    int kq = kqueue();
    if (kq == -1) return -1;
    return (int64_t)kq;
}

/*
 * sf_watch_add — Register a file or directory for watching.
 *
 * Opens the path with O_EVTONLY (a macOS-specific flag that opens the file
 * for event notification only, without preventing unmount) and registers it
 * with the kqueue for vnode events: write, delete, rename, and attribute
 * changes.
 *
 * Parameters:
 *   kq_fd — the kqueue fd returned by sf_watch_init()
 *   path  — null-terminated path to the file or directory to watch
 *
 * Returns: the watched file descriptor on success, -1 on error.
 */
int64_t sf_watch_add(int64_t kq_fd, const char *path) {
    if (kq_fd < 0 || path == NULL) return -1;

    int fd = open(path, O_EVTONLY);
    if (fd == -1) return -1;

    struct kevent change;
    EV_SET(&change, fd, EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB,
           0, NULL);

    int ret = kevent((int)kq_fd, &change, 1, NULL, 0, NULL);
    if (ret == -1) {
        close(fd);
        return -1;
    }

    return (int64_t)fd;
}

/*
 * sf_watch_poll — Poll for file system events with a timeout.
 *
 * Calls kevent() with the specified timeout to check for pending vnode
 * events on any watched file descriptors.
 *
 * Parameters:
 *   kq_fd      — the kqueue fd
 *   timeout_ms — maximum time to wait in milliseconds
 *                (0 = non-blocking check, no negative values)
 *
 * Returns:
 *   > 0 : the file descriptor that triggered an event
 *   0   : no events occurred within the timeout (timeout expired)
 *   -1  : error
 */
int64_t sf_watch_poll(int64_t kq_fd, int64_t timeout_ms) {
    if (kq_fd < 0) return -1;

    struct timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;

    struct kevent event;
    int n = kevent((int)kq_fd, NULL, 0, &event, 1, &ts);

    if (n < 0) {
        if (errno == EINTR) return 0;  /* Interrupted, treat as timeout */
        return -1;
    }
    if (n == 0) return 0;  /* Timeout, no events */

    /* Return the fd (ident) that triggered the event */
    return (int64_t)event.ident;
}

/*
 * sf_watch_remove — Remove a watch and close the watched file descriptor.
 *
 * Removes the kevent registration for the given fd and closes it. After
 * this call, no further events will be delivered for this fd.
 *
 * Parameters:
 *   kq_fd    — the kqueue fd
 *   watch_fd — the fd returned by sf_watch_add() to stop watching
 */
void sf_watch_remove(int64_t kq_fd, int64_t watch_fd) {
    if (kq_fd < 0 || watch_fd < 0) return;

    /* Remove the kevent registration */
    struct kevent change;
    EV_SET(&change, (uintptr_t)watch_fd, EVFILT_VNODE,
           EV_DELETE, 0, 0, NULL);
    kevent((int)kq_fd, &change, 1, NULL, 0, NULL);

    /* Close the watched file descriptor */
    close((int)watch_fd);
}

#elif defined(__linux__)

#include <sys/inotify.h>
#include <poll.h>
#include <limits.h>

/*
 * sf_watch_init — Create an inotify instance for watching files.
 *
 * Creates a non-blocking inotify file descriptor. The IN_NONBLOCK flag
 * ensures that read() calls will not block, allowing integration with the
 * cooperative scheduler.
 *
 * Returns: inotify fd on success, -1 on error.
 */
int64_t sf_watch_init(void) {
    int fd = inotify_init1(IN_NONBLOCK);
    if (fd == -1) return -1;
    return (int64_t)fd;
}

/*
 * sf_watch_add — Register a file or directory for watching via inotify.
 *
 * Adds a watch for the given path, monitoring for modifications, creations,
 * deletions, and moves. For directories, events are generated for files
 * within the directory.
 *
 * Parameters:
 *   kq_fd — the inotify fd returned by sf_watch_init()
 *   path  — null-terminated path to the file or directory to watch
 *
 * Returns: the watch descriptor on success, -1 on error.
 */
int64_t sf_watch_add(int64_t kq_fd, const char *path) {
    if (kq_fd < 0 || path == NULL) return -1;

    int wd = inotify_add_watch((int)kq_fd, path,
                               IN_MODIFY | IN_CREATE | IN_DELETE |
                               IN_MOVED_FROM | IN_MOVED_TO);
    if (wd == -1) return -1;
    return (int64_t)wd;
}

/*
 * sf_watch_poll — Poll for file system events with a timeout.
 *
 * Uses poll() to wait for readability on the inotify fd, then reads one
 * event to determine which watch descriptor triggered.
 *
 * Parameters:
 *   kq_fd      — the inotify fd
 *   timeout_ms — maximum time to wait in milliseconds
 *                (0 = non-blocking check)
 *
 * Returns:
 *   > 0 : the watch descriptor that triggered an event
 *   0   : no events occurred within the timeout
 *   -1  : error
 */
int64_t sf_watch_poll(int64_t kq_fd, int64_t timeout_ms) {
    if (kq_fd < 0) return -1;

    struct pollfd pfd;
    pfd.fd = (int)kq_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, (int)timeout_ms);
    if (ret < 0) {
        if (errno == EINTR) return 0;  /* Interrupted, treat as timeout */
        return -1;
    }
    if (ret == 0) return 0;  /* Timeout, no events */

    /* Read one inotify event */
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    ssize_t n = read((int)kq_fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    if (n < (ssize_t)sizeof(struct inotify_event)) return -1;

    struct inotify_event *event = (struct inotify_event *)buf;
    return (int64_t)event->wd;
}

/*
 * sf_watch_remove — Remove a watch descriptor from the inotify instance.
 *
 * Removes the inotify watch. After this call, no further events will be
 * delivered for this watch descriptor.
 *
 * Parameters:
 *   kq_fd    — the inotify fd
 *   watch_fd — the watch descriptor returned by sf_watch_add()
 */
void sf_watch_remove(int64_t kq_fd, int64_t watch_fd) {
    if (kq_fd < 0 || watch_fd < 0) return;
    inotify_rm_watch((int)kq_fd, (int)watch_fd);
}

#endif
