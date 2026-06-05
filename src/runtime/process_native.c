/*
 * Saffron Runtime: Subprocess Primitives
 * =======================================
 *
 * This module provides low-level subprocess management for the Saffron
 * native compiled runtime. All functions use the sf_process_ prefix and
 * are designed to be called from LLVM IR via `declare` directives.
 *
 * Design:
 *   Uses posix_spawn (preferred on macOS/Linux) to create child processes
 *   with full pipe control over stdin, stdout, and stderr. A handle table
 *   (similar to socket_native.c's TLS table) tracks up to 64 concurrent
 *   child processes.
 *
 * Non-blocking I/O:
 *   Pipe file descriptors can be set to non-blocking mode for integration
 *   with the cooperative scheduler. The sf_process_read_nonblock function
 *   returns immediately if no data is available.
 *
 * Platform: macOS (Darwin) and Linux. Both use POSIX APIs.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>

extern char **environ;

/* ===== Process Handle Table ===== */

#define SF_MAX_PROC_HANDLES 64

/* Flags for sf_process_spawn */
#define SF_PROC_PIPE_STDIN  1
#define SF_PROC_PIPE_STDOUT 2
#define SF_PROC_PIPE_STDERR 4
#define SF_PROC_SHELL       8

typedef struct {
    int in_use;
    pid_t pid;
    int stdin_fd;    /* write end — parent writes here */
    int stdout_fd;   /* read end — parent reads here */
    int stderr_fd;   /* read end — parent reads here */
    int exit_code;
    int is_done;
} sf_proc_entry_t;

static sf_proc_entry_t proc_table[SF_MAX_PROC_HANDLES];
static int proc_table_initialized = 0;

/* ===== Internal Helpers ===== */

static void proc_table_init(void) {
    if (!proc_table_initialized) {
        memset(proc_table, 0, sizeof(proc_table));
        proc_table_initialized = 1;
    }
}

/* Allocate a slot in the process handle table. Returns 1-based index or -1. */
static int64_t proc_table_alloc(void) {
    proc_table_init();
    for (int i = 0; i < SF_MAX_PROC_HANDLES; i++) {
        if (!proc_table[i].in_use) {
            memset(&proc_table[i], 0, sizeof(sf_proc_entry_t));
            proc_table[i].in_use = 1;
            proc_table[i].stdin_fd = -1;
            proc_table[i].stdout_fd = -1;
            proc_table[i].stderr_fd = -1;
            proc_table[i].exit_code = -1;
            proc_table[i].is_done = 0;
            return (int64_t)(i + 1);
        }
    }
    return -1;  /* table full */
}

/* Get a process entry from a handle. Returns NULL if invalid. */
static sf_proc_entry_t *proc_table_get(int64_t handle) {
    if (handle < 1 || handle > SF_MAX_PROC_HANDLES) return NULL;
    sf_proc_entry_t *entry = &proc_table[handle - 1];
    if (!entry->in_use) return NULL;
    return entry;
}

/* Set a file descriptor to non-blocking mode. */
static int set_nonblocking_fd(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Check if a process has exited without blocking. Updates entry if done. */
static void proc_check_exit(sf_proc_entry_t *entry) {
    if (entry->is_done) return;

    int status = 0;
    pid_t result = waitpid(entry->pid, &status, WNOHANG);
    if (result > 0) {
        entry->is_done = 1;
        if (WIFEXITED(status)) {
            entry->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            entry->exit_code = 128 + WTERMSIG(status);
        } else {
            entry->exit_code = -1;
        }
    }
}

/* ===== Public API ===== */

/*
 * Spawn a new child process with optional pipe control.
 *
 * program: path or name of executable (if SF_PROC_SHELL, this is the command string)
 * argv:    NULL-terminated array of arguments (NULL if SF_PROC_SHELL)
 * envp:    NULL-terminated array of "KEY=VALUE" strings (NULL for inherit)
 * cwd:     working directory (NULL or empty string for inherit)
 * flags:   bitmask of SF_PROC_PIPE_STDIN, SF_PROC_PIPE_STDOUT,
 *          SF_PROC_PIPE_STDERR, SF_PROC_SHELL
 *
 * Returns: process handle (>0) on success, -1 on error.
 */
int64_t sf_process_spawn(const char *program, const char **argv,
                         const char **envp, const char *cwd, int64_t flags) {
    if (program == NULL) return -1;

    proc_table_init();

    int64_t handle = proc_table_alloc();
    if (handle < 0) return -1;

    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return -1;

    /* Create pipes as needed */
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (flags & SF_PROC_PIPE_STDIN) {
        if (pipe(stdin_pipe) != 0) goto fail;
    }
    if (flags & SF_PROC_PIPE_STDOUT) {
        if (pipe(stdout_pipe) != 0) goto fail;
    }
    if (flags & SF_PROC_PIPE_STDERR) {
        if (pipe(stderr_pipe) != 0) goto fail;
    }

    /* Set up posix_spawn file actions */
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    if (flags & SF_PROC_PIPE_STDIN) {
        /* Child reads from stdin_pipe[0], parent writes to stdin_pipe[1] */
        posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdin_pipe[1]);
    }
    if (flags & SF_PROC_PIPE_STDOUT) {
        /* Child writes to stdout_pipe[1], parent reads from stdout_pipe[0] */
        posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
    }
    if (flags & SF_PROC_PIPE_STDERR) {
        /* Child writes to stderr_pipe[1], parent reads from stderr_pipe[0] */
        posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);
    }

    /* Handle working directory change via chdir file action */
    if (cwd != NULL && cwd[0] != '\0') {
        posix_spawn_file_actions_addchdir_np(&actions, cwd);
    }

    /* Build argv for shell mode */
    const char *spawn_program;
    const char *shell_argv[4];

    if (flags & SF_PROC_SHELL) {
        spawn_program = "/bin/sh";
        shell_argv[0] = "sh";
        shell_argv[1] = "-c";
        shell_argv[2] = program;
        shell_argv[3] = NULL;
        argv = shell_argv;
    } else {
        spawn_program = program;
        if (argv == NULL) {
            /* Default: just the program name */
            shell_argv[0] = program;
            shell_argv[1] = NULL;
            argv = shell_argv;
        }
    }

    /* Spawn the child process */
    char **env_to_use = (envp != NULL) ? (char **)envp : environ;

    pid_t pid;
    int spawn_ret;

    if (flags & SF_PROC_SHELL) {
        /* Shell mode: use absolute path to /bin/sh */
        spawn_ret = posix_spawn(&pid, spawn_program, &actions, NULL,
                                (char *const *)argv, env_to_use);
    } else {
        /* Direct mode: search PATH */
        spawn_ret = posix_spawnp(&pid, spawn_program, &actions, NULL,
                                 (char *const *)argv, env_to_use);
    }

    posix_spawn_file_actions_destroy(&actions);

    if (spawn_ret != 0) {
        goto fail;
    }

    /* Close child ends of pipes in the parent */
    if (flags & SF_PROC_PIPE_STDIN) {
        close(stdin_pipe[0]);
        entry->stdin_fd = stdin_pipe[1];
    }
    if (flags & SF_PROC_PIPE_STDOUT) {
        close(stdout_pipe[1]);
        entry->stdout_fd = stdout_pipe[0];
        /* Set stdout pipe to non-blocking for read_available */
        set_nonblocking_fd(entry->stdout_fd);
    }
    if (flags & SF_PROC_PIPE_STDERR) {
        close(stderr_pipe[1]);
        entry->stderr_fd = stderr_pipe[0];
        /* Set stderr pipe to non-blocking for read_available */
        set_nonblocking_fd(entry->stderr_fd);
    }

    entry->pid = pid;
    return handle;

fail:
    /* Clean up pipes on failure */
    if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
    if (stdout_pipe[0] >= 0) { close(stdout_pipe[0]); close(stdout_pipe[1]); }
    if (stderr_pipe[0] >= 0) { close(stderr_pipe[0]); close(stderr_pipe[1]); }
    entry->in_use = 0;
    return -1;
}

/*
 * Write data to the stdin pipe of a child process.
 *
 * Returns: bytes written on success, -1 on error.
 */
int64_t sf_process_write_stdin(int64_t handle, const char *data, int64_t len) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL || entry->stdin_fd < 0) return -1;
    if (data == NULL || len <= 0) return -1;

    ssize_t n = write(entry->stdin_fd, data, (size_t)len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return (int64_t)n;
}

/*
 * Read data from the stdout pipe of a child process (blocking).
 *
 * Temporarily sets the fd to blocking mode for this read, then restores
 * non-blocking mode. Returns bytes read, 0 on EOF, -1 on error.
 */
int64_t sf_process_read_stdout(int64_t handle, char *buf, int64_t max_len) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL || entry->stdout_fd < 0) return -1;
    if (buf == NULL || max_len <= 0) return -1;

    /* Temporarily set to blocking */
    int flags = fcntl(entry->stdout_fd, F_GETFL, 0);
    fcntl(entry->stdout_fd, F_SETFL, flags & ~O_NONBLOCK);

    ssize_t n = read(entry->stdout_fd, buf, (size_t)max_len);

    /* Restore non-blocking */
    fcntl(entry->stdout_fd, F_SETFL, flags);

    if (n < 0) return -1;
    return (int64_t)n;
}

/*
 * Read data from the stderr pipe of a child process (blocking).
 *
 * Returns bytes read, 0 on EOF, -1 on error.
 */
int64_t sf_process_read_stderr(int64_t handle, char *buf, int64_t max_len) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL || entry->stderr_fd < 0) return -1;
    if (buf == NULL || max_len <= 0) return -1;

    /* Temporarily set to blocking */
    int flags = fcntl(entry->stderr_fd, F_GETFL, 0);
    fcntl(entry->stderr_fd, F_SETFL, flags & ~O_NONBLOCK);

    ssize_t n = read(entry->stderr_fd, buf, (size_t)max_len);

    /* Restore non-blocking */
    fcntl(entry->stderr_fd, F_SETFL, flags);

    if (n < 0) return -1;
    return (int64_t)n;
}

/*
 * Non-blocking read from a child process pipe.
 *
 * stream: 1 = stdout, 2 = stderr
 *
 * Returns:
 *   > 0  : bytes read
 *   0    : would block (no data available yet)
 *   -1   : EOF or pipe closed
 *   -2   : error
 */
int64_t sf_process_read_nonblock(int64_t handle, int64_t stream,
                                 char *buf, int64_t max_len) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return -2;
    if (buf == NULL || max_len <= 0) return -2;

    int fd;
    if (stream == 1) {
        fd = entry->stdout_fd;
    } else if (stream == 2) {
        fd = entry->stderr_fd;
    } else {
        return -2;
    }

    if (fd < 0) return -1;

    ssize_t n = read(fd, buf, (size_t)max_len);
    if (n > 0) return (int64_t)n;
    if (n == 0) return -1;  /* EOF */

    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    return -2;
}

/*
 * Poll a child process to check if it has exited.
 *
 * Returns: -1 if still running, exit code (>=0) if done.
 */
int64_t sf_process_poll(int64_t handle) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return -2;

    proc_check_exit(entry);

    if (entry->is_done) {
        return (int64_t)entry->exit_code;
    }
    return -1;  /* still running */
}

/*
 * Wait for a child process to exit (blocking).
 *
 * Returns: exit code on success, -1 on error.
 */
int64_t sf_process_wait(int64_t handle) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return -1;

    if (entry->is_done) {
        return (int64_t)entry->exit_code;
    }

    int status = 0;
    pid_t result = waitpid(entry->pid, &status, 0);
    if (result < 0) return -1;

    entry->is_done = 1;
    if (WIFEXITED(status)) {
        entry->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        entry->exit_code = 128 + WTERMSIG(status);
    } else {
        entry->exit_code = -1;
    }

    return (int64_t)entry->exit_code;
}

/*
 * Send a signal to a child process.
 *
 * Common signals: 9 = SIGKILL, 15 = SIGTERM, 2 = SIGINT
 */
void sf_process_kill(int64_t handle, int64_t signal_num) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return;
    if (entry->is_done) return;

    kill(entry->pid, (int)signal_num);
}

/*
 * Close all pipes and release the handle table slot.
 * Does NOT kill the child process — call sf_process_kill first if needed.
 */
void sf_process_close(int64_t handle) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return;

    if (entry->stdin_fd >= 0) {
        close(entry->stdin_fd);
        entry->stdin_fd = -1;
    }
    if (entry->stdout_fd >= 0) {
        close(entry->stdout_fd);
        entry->stdout_fd = -1;
    }
    if (entry->stderr_fd >= 0) {
        close(entry->stderr_fd);
        entry->stderr_fd = -1;
    }

    /* Reap zombie if not already done */
    if (!entry->is_done) {
        int status = 0;
        waitpid(entry->pid, &status, WNOHANG);
    }

    entry->in_use = 0;
}

/*
 * Close just the stdin pipe (signal EOF to child).
 * Used when you're done writing input to the child.
 */
void sf_process_close_stdin(int64_t handle) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return;

    if (entry->stdin_fd >= 0) {
        close(entry->stdin_fd);
        entry->stdin_fd = -1;
    }
}

/*
 * Get the PID of a child process.
 *
 * Returns: PID on success, -1 on error.
 */
int64_t sf_process_pid(int64_t handle) {
    sf_proc_entry_t *entry = proc_table_get(handle);
    if (entry == NULL) return -1;
    return (int64_t)entry->pid;
}
