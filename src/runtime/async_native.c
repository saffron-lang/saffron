// Saffron Async Runtime — native helpers for the scheduler
// These are the only C functions needed; the scheduler itself is pure Saffron.

#include <time.h>
#include <sys/select.h>
#include <stdint.h>

// sf_time_now() -> double (seconds since monotonic epoch)
double sf_time_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// sf_select_fds(read_fds, read_count, write_fds, write_count, timeout_ms)
//
// Polls multiple file descriptors for readiness.
// read_fds/write_fds: pointers to arrays of i64 fd values
// read_count/write_count: number of fds in each array
// timeout_ms: milliseconds to wait (0 = non-blocking, -1 = infinite)
//
// Returns a bitmask packed into i64:
//   bits 0-31: bitmask of read_fds that are ready (fd at index i → bit i)
//   bits 32-63: bitmask of write_fds that are ready (fd at index i → bit i)
//   Returns 0 if timeout, -1 on error.
//
// Supports up to 32 read fds + 32 write fds (plenty for cooperative async).
int64_t sf_select_fds(int64_t *read_fds, int64_t read_count,
                      int64_t *write_fds, int64_t write_count,
                      int64_t timeout_ms) {
    fd_set rfds, wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    int maxfd = 0;

    for (int i = 0; i < read_count && i < 32; i++) {
        int fd = (int)read_fds[i];
        if (fd >= 0) {
            FD_SET(fd, &rfds);
            if (fd > maxfd) maxfd = fd;
        }
    }

    for (int i = 0; i < write_count && i < 32; i++) {
        int fd = (int)write_fds[i];
        if (fd >= 0) {
            FD_SET(fd, &wfds);
            if (fd > maxfd) maxfd = fd;
        }
    }

    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }

    int ret = select(maxfd + 1, &rfds, &wfds, NULL, tvp);
    if (ret < 0) return -1;
    if (ret == 0) return 0;

    int64_t result = 0;

    for (int i = 0; i < read_count && i < 32; i++) {
        int fd = (int)read_fds[i];
        if (fd >= 0 && FD_ISSET(fd, &rfds)) {
            result |= ((int64_t)1 << i);
        }
    }

    for (int i = 0; i < write_count && i < 32; i++) {
        int fd = (int)write_fds[i];
        if (fd >= 0 && FD_ISSET(fd, &wfds)) {
            result |= ((int64_t)1 << (32 + i));
        }
    }

    return result;
}

// --- Yield reason globals and coro stubs ---
// These globals are defined in base.ll; we extern them here.
extern int64_t __yield_reason;
extern int64_t __yield_arg;

int64_t __sched_get_yield_reason(void) { return __yield_reason; }
int64_t __sched_get_yield_arg(void) { return __yield_arg; }
void __sched_reset_yield(void) { __yield_reason = 0; __yield_arg = 0; }

// Coroutine frame layout after LLVM CoroSplit:
//   offset 0: resume function pointer (void (*)(ptr frame))
//   offset 8: destroy function pointer (void (*)(ptr frame))
// After final suspend, the resume pointer is set to a special value
// and the "done" bit is indicated by the index field.
//
// LLVM's coro.done checks if frame->resume == nullptr (or a sentinel).
// We match that behavior.

typedef void (*coro_fn_t)(void *);

void __sched_coro_resume(void *hdl) {
    coro_fn_t resume_fn = *(coro_fn_t *)hdl;
    resume_fn(hdl);
}

int64_t __sched_coro_done(void *hdl) {
    coro_fn_t resume_fn = *(coro_fn_t *)hdl;
    return (resume_fn == (coro_fn_t)0) ? 1 : 0;
}

void __sched_coro_destroy(void *hdl) {
    coro_fn_t *fn_ptrs = (coro_fn_t *)hdl;
    coro_fn_t destroy_fn = fn_ptrs[1];
    destroy_fn(hdl);
}
