/*
 * Saffron Runtime: Thread Primitives (Workstream A)
 * =================================================
 *
 * Host-only OS threads for the native runtime — spawn/join/detach/sleep — under
 * a Global Runtime Lock (GRL). All exports use the sf_thread_ prefix and take/
 * return untagged int64_t (CLAUDE.md: extern params must be untagged), and are
 * declared to LLVM via @extern in src/lib/thread.sf.
 *
 * ── Why a GRL, in one paragraph ────────────────────────────────────────────
 * The garbage collector is a set of process-global LLVM globals with no locking
 * (src/runtime/gc.ll): one free-list head, one shadow stack, byte counters, and
 * @__gc_alloc triggers an inline mark-sweep. Two OS threads allocating at once is
 * routine heap corruption, not a rare race. So v1 runs only ONE thread of managed
 * (heap-touching) Saffron code at a time, serialized by the GRL. The lock is held
 * for the whole body of every thread and released ONLY around a blocking native
 * call (join, sleep, and later mutex/condvar waits). This buys blocking-FFI/IO
 * concurrency without touching the GC; true CPU parallelism is the separately-
 * scoped v2 (see docs/design/threading-module-plan.md §0).
 *
 * ── The invariant ──────────────────────────────────────────────────────────
 * You may not touch a heap object or allocate unless you hold the GRL. Every
 * blocking primitive is "drop GRL, block, re-take GRL": if a blocking call kept
 * the lock, the thread it is waiting on could never acquire it to make progress —
 * a guaranteed deadlock (see sf_thread_join).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

/* ===== The Global Runtime Lock ===== */

/*
 * Recursive so a thread already holding the GRL (the normal running state) can
 * call a primitive that also takes it without self-deadlocking. The lock is
 * initialized lazily under a once-guard because there is no runtime init hook we
 * control from C; sf_grl_lock() is called from the process entry preamble (see
 * src/lib/thread.sf and the codegen entry note) before any thread is spawned.
 */
static pthread_mutex_t sf_grl;
static pthread_once_t sf_grl_once = PTHREAD_ONCE_INIT;

static void sf_grl_init(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&sf_grl, &attr);
    pthread_mutexattr_destroy(&attr);
}

void sf_grl_lock(void) {
    pthread_once(&sf_grl_once, sf_grl_init);
    pthread_mutex_lock(&sf_grl);
}

void sf_grl_unlock(void) {
    pthread_mutex_unlock(&sf_grl);
}

/*
 * The main thread must hold the GRL before the FIRST worker can run, or that
 * worker races the main thread on the heap. Rather than a codegen entry hook
 * (plan §6), acquire it lazily, exactly once, the first time a thread is spawned:
 * at that instant no other thread exists yet, so there is no window to race, and
 * from then on the GIL invariant holds. Self-contained, no .ll change.
 *
 * Runs on the main (spawning) thread because pthread_once executes the init on
 * whichever thread first reaches it, and the first spawn is on the main thread.
 */
static pthread_once_t sf_main_grl_once = PTHREAD_ONCE_INIT;
static void sf_main_grl_acquire(void) { sf_grl_lock(); }

/* ===== Thread Handle Table ===== */

#define SF_MAX_THREADS 256

typedef struct {
    int in_use;
    pthread_t tid;
    int64_t closure;   /* untagged ptr to the [fn_ptr, env] closure box */
    int64_t result;    /* the closure's return value, published under the GRL */
    int done;          /* 1 once the worker has finished; read under the GRL */
    int detached;      /* 1 if detached — join is illegal and the slot self-frees */
    int joined;        /* 1 once joined, so a double join is a no-op not a crash */
} sf_thread_entry_t;

static sf_thread_entry_t thread_table[SF_MAX_THREADS];
static int thread_table_initialized = 0;

/*
 * The table itself is shared mutable state touched by spawn (main thread) and the
 * worker trampoline, so it has its own small mutex — NOT the GRL. Using the GRL
 * here would be wrong: the trampoline must publish `done`/`result` while NOT
 * holding the GRL (it releases it before exit so a joiner can wake), so table
 * writes need their own lock independent of GRL ownership.
 */
static pthread_mutex_t thread_table_lock = PTHREAD_MUTEX_INITIALIZER;

static void thread_table_init(void) {
    if (!thread_table_initialized) {
        memset(thread_table, 0, sizeof(thread_table));
        thread_table_initialized = 1;
    }
}

/* Allocate a slot; returns a 1-based handle or -1 if the table is full. */
static int64_t thread_table_alloc(void) {
    int64_t h = -1;
    pthread_mutex_lock(&thread_table_lock);
    thread_table_init();
    for (int i = 0; i < SF_MAX_THREADS; i++) {
        if (!thread_table[i].in_use) {
            memset(&thread_table[i], 0, sizeof(sf_thread_entry_t));
            thread_table[i].in_use = 1;
            h = i + 1;
            break;
        }
    }
    pthread_mutex_unlock(&thread_table_lock);
    return h;
}

/* Resolve a 1-based handle to its entry, or NULL if out of range / freed. */
static sf_thread_entry_t *thread_entry(int64_t handle) {
    if (handle < 1 || handle > SF_MAX_THREADS) return NULL;
    sf_thread_entry_t *e = &thread_table[handle - 1];
    if (!e->in_use) return NULL;
    return e;
}

/* ===== The worker trampoline ===== */

/*
 * Runs on the new OS thread. Acquires the GRL before touching any heap object,
 * calls the Saffron closure exactly as codegen would — fn(env), with env passed
 * through UNCHANGED (it is a NaN-boxed value; codegen's own closure call does not
 * untag it, so neither do we) — publishes the result, and drops the GRL.
 */
static void *sf_thread_trampoline(void *arg) {
    int64_t handle = (int64_t)(intptr_t)arg;

    /* Read the closure pointer without needing the GRL: it was set before the
     * thread was created and never mutated after. */
    pthread_mutex_lock(&thread_table_lock);
    sf_thread_entry_t *e = thread_entry(handle);
    int64_t closure = e ? e->closure : 0;
    pthread_mutex_unlock(&thread_table_lock);
    if (!closure) return NULL;

    int64_t *pair = (int64_t *)closure;               /* [fn_ptr, env] */
    int64_t (*fn)(int64_t) = (int64_t (*)(int64_t))pair[0];
    int64_t env = pair[1];

    sf_grl_lock();                                     /* enter managed code */
    int64_t r = fn(env);
    sf_grl_unlock();                                   /* leave managed code */

    /* Publish result/done under the table lock. A detached thread frees its own
     * slot here since no one will join it. */
    pthread_mutex_lock(&thread_table_lock);
    e = thread_entry(handle);
    if (e) {
        e->result = r;
        e->done = 1;
        if (e->detached) {
            e->in_use = 0;   /* self-free: detached threads are never joined */
        }
    }
    pthread_mutex_unlock(&thread_table_lock);
    return NULL;
}

/* ===== Public API ===== */

/*
 * sf_thread_spawn — Start a worker running `closure` ([fn_ptr, env]).
 *
 * `closure` is the untagged closure-box pointer. Returns a 1-based handle, or -1
 * if the table is full or pthread_create fails. The caller (thread.sf) holds the
 * GRL; the worker will block on it inside the trampoline until the caller next
 * releases it (at join, sleep, or its own exit) — so the worker cannot corrupt
 * the heap by running concurrently with the spawner.
 */
int64_t sf_thread_spawn(int64_t closure) {
    /* Take the GRL for the main thread before any worker exists (see above). */
    pthread_once(&sf_main_grl_once, sf_main_grl_acquire);

    int64_t handle = thread_table_alloc();
    if (handle < 0) return -1;

    pthread_mutex_lock(&thread_table_lock);
    sf_thread_entry_t *e = thread_entry(handle);
    if (e) e->closure = closure;
    pthread_mutex_unlock(&thread_table_lock);

    pthread_t tid;
    if (pthread_create(&tid, NULL, sf_thread_trampoline,
                       (void *)(intptr_t)handle) != 0) {
        pthread_mutex_lock(&thread_table_lock);
        e = thread_entry(handle);
        if (e) e->in_use = 0;
        pthread_mutex_unlock(&thread_table_lock);
        return -1;
    }

    pthread_mutex_lock(&thread_table_lock);
    e = thread_entry(handle);
    if (e) e->tid = tid;
    pthread_mutex_unlock(&thread_table_lock);
    return handle;
}

/*
 * sf_thread_join — Wait for a worker to finish and return its result.
 *
 * CRITICAL GRL discipline: the caller holds the GRL; we DROP it around
 * pthread_join and re-take it after. If we held it, the worker could never
 * acquire the GRL to run its body, so it would never finish, so pthread_join
 * would block forever — a deadlock. Dropping the GRL lets the worker run; when it
 * finishes it releases the GRL and we re-acquire it before returning to managed
 * code. Returns the closure's result (a NaN-boxed value, untouched), or 0 for a
 * bad/detached handle.
 */
int64_t sf_thread_join(int64_t handle) {
    pthread_mutex_lock(&thread_table_lock);
    sf_thread_entry_t *e = thread_entry(handle);
    if (!e || e->detached || e->joined) {
        pthread_mutex_unlock(&thread_table_lock);
        return 0;
    }
    pthread_t tid = e->tid;
    e->joined = 1;
    pthread_mutex_unlock(&thread_table_lock);

    sf_grl_unlock();                 /* let the worker make progress */
    pthread_join(tid, NULL);
    sf_grl_lock();                   /* back into managed code */

    int64_t result = 0;
    pthread_mutex_lock(&thread_table_lock);
    e = thread_entry(handle);
    if (e) {
        result = e->result;
        e->in_use = 0;               /* joined: slot is free to reuse */
    }
    pthread_mutex_unlock(&thread_table_lock);
    return result;
}

/*
 * sf_thread_detach — Give up the right to join `handle`. The worker's slot is
 * freed automatically when it finishes (or now, if it already has). Returns 0.
 */
int64_t sf_thread_detach(int64_t handle) {
    pthread_mutex_lock(&thread_table_lock);
    sf_thread_entry_t *e = thread_entry(handle);
    if (e) {
        pthread_detach(e->tid);
        if (e->done) {
            e->in_use = 0;           /* already finished: free now */
        } else {
            e->detached = 1;         /* trampoline will free on exit */
        }
    }
    pthread_mutex_unlock(&thread_table_lock);
    return 0;
}

/*
 * sf_thread_sleep — Sleep the current thread for `seconds`, releasing the GRL so
 * other threads run meanwhile. This is a real OS sleep, distinct from the
 * scheduler's cooperative Async.sleep (which yields a coroutine). nanosleep is
 * retried across EINTR.
 */
void sf_thread_sleep(double seconds) {
    if (seconds < 0) seconds = 0;
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);

    sf_grl_unlock();
    struct timespec rem;
    while (nanosleep(&ts, &rem) != 0 && errno == EINTR) {
        ts = rem;
    }
    sf_grl_lock();
}
