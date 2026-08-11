/*
 * Saffron Runtime: Terminal (TTY) Control Primitives
 * ==================================================
 *
 * Host-only (native) support for building full-screen terminal UIs: raw mode,
 * terminal size, and non-blocking stdin reads. This is the one low-level piece
 * a TUI needs that the rest of the runtime does not already provide.
 *
 * RAW MODE. A cooked terminal buffers a whole line, echoes keystrokes, and
 * interprets Ctrl-C/Ctrl-Z itself. A TUI wants none of that: it reads bytes as
 * they arrive, draws its own cursor, and handles Ctrl-C as an ordinary key (or
 * lets @signal deliver SIGINT). sf_tty_raw_mode(1) puts the terminal into the
 * classic cfmakeraw() state and STASHES the previous termios so (0) restores it
 * exactly. The stash is a single static: a process has one controlling terminal
 * and one UI in front of it, so one save slot is correct and keeps restore
 * dead simple even from an atexit/signal path.
 *
 * SIZE. sf_tty_size() asks the kernel via ioctl(TIOCGWINSZ) and packs rows/cols
 * into one i64 (rows in the high 32 bits, cols in the low 32) so the Saffron
 * side gets both from one extern call without out-params. SIGWINCH delivery —
 * "the size changed" — is NOT here: it rides @signal's existing self-pipe, and
 * the handler just calls sf_tty_size() again.
 *
 * INPUT. sf_tty_read_input() is a select()-gated read on fd 0 with a millisecond
 * timeout, so the UI's event loop can wait for a keystroke without spinning and
 * without blocking forever (letting timers/animation still tick). It returns the
 * byte count (0 on timeout, -1 on error); the escape-sequence parser on the
 * Saffron side turns those bytes into key/mouse events.
 *
 * Portable across macOS and Linux: termios + ioctl + select, all POSIX.
 */

#include <stdint.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>

/*
 * The saved cooked-mode termios. is_raw guards against a double-enter losing the
 * original state (enter, enter, exit would otherwise restore the raw state as if
 * it were cooked). One process, one terminal, one save slot.
 */
static struct termios sf_tty_saved;
static int sf_tty_is_raw = 0;

/*
 * sf_tty_raw_mode — Enter (on != 0) or leave (on == 0) raw mode on the
 * controlling terminal (fd 0). Idempotent in both directions.
 *
 * Returns 0 on success, -1 on failure (not a tty, or tcsetattr rejected it).
 */
int64_t sf_tty_raw_mode(int64_t on) {
    if (on) {
        if (sf_tty_is_raw) return 0;
        if (tcgetattr(STDIN_FILENO, &sf_tty_saved) != 0) return -1;

        struct termios raw = sf_tty_saved;
        /* cfmakeraw() is available on both macOS and glibc/musl. */
        cfmakeraw(&raw);
        /* Non-blocking-ish read semantics: return as soon as >=1 byte is ready,
         * but do not block if select() said readable and it turns out empty.
         * VMIN=1/VTIME=0 = block until at least one byte; the select() gate in
         * sf_tty_read_input provides the timeout, so this stays a blocking read
         * only AFTER select says data is ready. */
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
        sf_tty_is_raw = 1;
        return 0;
    } else {
        if (!sf_tty_is_raw) return 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &sf_tty_saved) != 0) return -1;
        sf_tty_is_raw = 0;
        return 0;
    }
}

/*
 * sf_tty_size — Query the terminal window size.
 *
 * Returns rows and cols packed into one i64: (rows << 32) | cols. On failure
 * (not a tty, ioctl unsupported) returns 0, which the caller reads as 0x0 and
 * can fall back to a default (e.g. 80x24).
 */
int64_t sf_tty_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
        /* Try stdin too — stdout may be redirected while stdin is the tty. */
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0) return 0;
    }
    int64_t rows = (int64_t)ws.ws_row;
    int64_t cols = (int64_t)ws.ws_col;
    return (rows << 32) | (cols & 0xFFFFFFFF);
}

/*
 * sf_tty_read_input — Read up to max_len bytes of terminal input into buf,
 * waiting at most timeout_ms milliseconds for the first byte.
 *
 * A negative timeout_ms blocks indefinitely (wait for a key). A zero timeout
 * polls (return immediately with whatever is buffered, or 0). This is the one
 * call the event loop parks on each frame.
 *
 * Returns: the number of bytes read (>= 1), 0 on timeout (no input arrived),
 * or -1 on error.
 */
int64_t sf_tty_read_input(int64_t buf, int64_t max_len, int64_t timeout_ms) {
    if (max_len <= 0) return 0;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);

    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }

    int sel;
    do {
        sel = select(STDIN_FILENO + 1, &rfds, NULL, NULL, tvp);
    } while (sel < 0 && errno == EINTR);

    if (sel < 0) return -1;   /* real select error */
    if (sel == 0) return 0;   /* timeout, no input */

    ssize_t n;
    do {
        n = read(STDIN_FILENO, (void *)(uintptr_t)buf, (size_t)max_len);
    } while (n < 0 && errno == EINTR);

    if (n < 0) return -1;
    return (int64_t)n;
}

/*
 * sf_tty_write — Write len bytes from buf directly to stdout (fd 1).
 *
 * The renderer builds one frame's worth of ANSI as a single string and hands it
 * here. write(2) is unbuffered, so a frame reaches the terminal atomically with
 * no fflush() dance and no dependence on the platform's stdout FILE* symbol
 * (__stdoutp vs stdout). Loops on partial writes so a large frame is fully
 * emitted. Returns the total bytes written, or -1 on error.
 */
int64_t sf_tty_write(int64_t buf, int64_t len) {
    if (len <= 0) return 0;
    const char *p = (const char *)(uintptr_t)buf;
    int64_t total = 0;
    while (total < len) {
        ssize_t n = write(STDOUT_FILENO, p + total, (size_t)(len - total));
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += n;
    }
    return total;
}
