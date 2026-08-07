/*
 * Saffron Runtime: Signal Handling Primitives
 * ============================================
 *
 * Host-only (native) support for RECEIVING POSIX signals. Sending already
 * exists in @process (sf_process_kill); this is the other half.
 *
 * THE SELF-PIPE, AND WHY IT HAS TO BE ONE. A POSIX signal handler may call only
 * async-signal-safe functions (signal(7) / sigaction(2)) — it runs on a borrowed
 * stack at an arbitrary instruction boundary, so it cannot allocate, cannot take
 * a lock, and above all cannot run Saffron code (that would re-enter the GC and
 * the NaN-box runtime from inside an interrupt). The only safe thing it does here
 * is write() one byte — the signal number — into a pipe. write() is on the
 * async-signal-safe list; nothing else in the handler is not.
 *
 * That turns "a signal arrived" into "a byte is readable on an fd", which is
 * exactly the shape the cooperative scheduler already parks on for sockets
 * (yield reason 2, IO-read). So @signal needs NO scheduler changes: signal.sf
 * spawns a task that parks on the read end of this pipe via the normal IO path,
 * wakes when a byte lands, reads the signal number with sf_signal_next(), and
 * runs the user callback as ordinary Saffron code — outside the handler.
 *
 * The pipe's read end is set non-blocking so a spurious wake (the scheduler
 * polls the fd, another reader drained it) returns -1 rather than blocking the
 * whole single-threaded runtime. The write end is left blocking but the handler
 * ignores a full-pipe EAGAIN/short write: a coalesced duplicate of a signal that
 * is already pending delivery is not a lost signal.
 *
 * Portable across macOS and Linux: plain pipe(2) + sigaction(2), no signalfd or
 * kqueue EVFILT_SIGNAL, so one implementation serves both.
 */

#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/*
 * The self-pipe. -1 until sf_signal_init() runs. The write end is stored in a
 * sig_atomic_t because the handler reads it; the read end is only touched by the
 * normal (non-handler) code path.
 */
static int sf_sig_read_fd = -1;
static volatile sig_atomic_t sf_sig_write_fd = -1;

/*
 * The one function that runs in interrupt context. It must stay this small:
 * capture errno, write the signal number as a single byte, restore errno. A
 * short write or EAGAIN (pipe full) is deliberately ignored — the byte already
 * queued will wake the reader, and a signal number is <= 255 so one byte holds
 * it. errno is saved and restored because the interrupted code may be mid-way
 * through inspecting its own errno.
 */
static void sf_signal_handler(int signum) {
    int saved_errno = errno;
    int fd = (int)sf_sig_write_fd;
    if (fd >= 0) {
        unsigned char byte = (unsigned char)signum;
        ssize_t r;
        do {
            r = write(fd, &byte, 1);
        } while (r < 0 && errno == EINTR);
        /* EAGAIN / short write: the reader will still be woken by the pipe. */
    }
    errno = saved_errno;
}

/*
 * sf_signal_init — Create the self-pipe. Idempotent: a second call returns the
 * existing read fd rather than leaking a new pipe.
 *
 * Returns: the read-end fd on success (to be parked on by the scheduler), or -1
 * on failure.
 */
int64_t sf_signal_init(void) {
    if (sf_sig_read_fd >= 0) return (int64_t)sf_sig_read_fd;

    int fds[2];
    if (pipe(fds) != 0) return -1;

    /* Read end non-blocking: a spurious wake must not block the runtime. */
    int flags = fcntl(fds[0], F_GETFL, 0);
    if (flags != -1) fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

    sf_sig_read_fd = fds[0];
    sf_sig_write_fd = fds[1];
    return (int64_t)sf_sig_read_fd;
}

/*
 * sf_signal_register — Install the handler for one signal number.
 *
 * SA_RESTART so a signal does not turn every in-flight blocking syscall in the
 * runtime into an EINTR the caller has to handle. Returns 0 on success, -1 on
 * failure (bad signal number, or init not yet run).
 */
int64_t sf_signal_register(int64_t signum) {
    if (sf_sig_read_fd < 0) return -1;
    if (signum <= 0 || signum >= NSIG) return -1;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sf_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction((int)signum, &sa, NULL) != 0) return -1;
    return 0;
}

/*
 * sf_signal_read_fd — The read-end fd, for the scheduler to poll/park on.
 * Returns -1 before sf_signal_init().
 */
int64_t sf_signal_read_fd(void) {
    return (int64_t)sf_sig_read_fd;
}

/*
 * sf_signal_raise — Send a signal to the current process (raise(3)).
 *
 * Useful in its own right (a process asking itself to reload/quit) and it is how
 * the test suite exercises delivery without spawning a second process. Returns 0
 * on success, non-zero on failure.
 */
int64_t sf_signal_raise(int64_t signum) {
    return (int64_t)raise((int)signum);
}

/*
 * sf_signal_sigusr — The platform's SIGUSR1 (which==1) or SIGUSR2 (any other).
 *
 * These numbers differ across platforms (Linux 10/12, macOS/BSD 30/31), so the
 * stdlib asks C — which knows via <signal.h> — rather than hardcoding a guess.
 */
int64_t sf_signal_sigusr(int64_t which) {
    return (which == 1) ? (int64_t)SIGUSR1 : (int64_t)SIGUSR2;
}

/*
 * sf_signal_next — Drain ONE pending signal number from the pipe, non-blocking.
 *
 * Returns the signal number (> 0) if one was waiting, or -1 if none is (EAGAIN)
 * or on error. The caller loops on this after a wake to drain every byte, since
 * several signals may have coalesced into the readable state.
 */
int64_t sf_signal_next(void) {
    if (sf_sig_read_fd < 0) return -1;

    unsigned char byte;
    ssize_t r;
    do {
        r = read(sf_sig_read_fd, &byte, 1);
    } while (r < 0 && errno == EINTR);

    if (r == 1) return (int64_t)byte;
    return -1;  /* EAGAIN (nothing pending) or a real error: caller stops. */
}
