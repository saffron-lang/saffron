/*
 * Saffron Runtime: Non-blocking TCP + TLS Socket Primitives
 * =========================================================
 *
 * This module provides the low-level C foundation for networking in the
 * Saffron native compiled runtime. All functions use the sf_ prefix and
 * are designed to be called from LLVM IR via `declare` directives.
 *
 * Async story:
 *   All socket operations are non-blocking. When an operation would block,
 *   it returns a sentinel value (-1 for would_block) instead of waiting.
 *   This allows the Saffron-level scheduler to:
 *     1. Attempt an I/O operation
 *     2. If it would block, yield the current task
 *     3. Poll the fd for readiness on the next scheduler tick
 *     4. Resume the task once the fd is ready
 *   Until the cooperative I/O scheduler lands, the Saffron wrapper does
 *   synchronous poll-loops internally (see socket.sf).
 *
 * Platform: macOS (Darwin) and Linux. Both use POSIX socket APIs.
 * The only platform difference is that macOS uses SO_NOSIGPIPE while
 * Linux uses MSG_NOSIGNAL on send().
 *
 * TLS: Uses OpenSSL (libssl/libcrypto). A single global SSL_CTX is
 * lazily initialized on first use. TLS handles are indices into a
 * fixed-size table of SSL* pointers.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef __APPLE__
#include <sys/socket.h>  /* SO_NOSIGPIPE */
#endif

/* ===== TLS Handle Table ===== */

#define SF_MAX_TLS_HANDLES 64

static SSL *tls_table[SF_MAX_TLS_HANDLES];
static int  tls_table_initialized = 0;

/* Global OpenSSL context, lazily initialized */
static SSL_CTX *g_ssl_ctx = NULL;

/* ===== Internal Helpers ===== */

static void tls_table_init(void) {
    if (!tls_table_initialized) {
        memset(tls_table, 0, sizeof(tls_table));
        tls_table_initialized = 1;
    }
}

/* Allocate a slot in the TLS handle table. Returns index (1-based) or -1. */
static int64_t tls_table_alloc(SSL *ssl) {
    tls_table_init();
    for (int i = 0; i < SF_MAX_TLS_HANDLES; i++) {
        if (tls_table[i] == NULL) {
            tls_table[i] = ssl;
            return (int64_t)(i + 1);  /* 1-based so 0 means "no TLS" */
        }
    }
    return -1;  /* table full */
}

/* Retrieve SSL* from a handle. Returns NULL if invalid. */
static SSL *tls_table_get(int64_t handle) {
    if (handle < 1 || handle > SF_MAX_TLS_HANDLES) return NULL;
    return tls_table[handle - 1];
}

/* Free a slot in the TLS handle table. */
static void tls_table_free(int64_t handle) {
    if (handle >= 1 && handle <= SF_MAX_TLS_HANDLES) {
        tls_table[handle - 1] = NULL;
    }
}

/* Set a file descriptor to non-blocking mode. Returns 0 on success. */
static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Ensure the global SSL_CTX exists. Returns 0 on success, -1 on error. */
static int ensure_ssl_ctx(void) {
    if (g_ssl_ctx != NULL) return 0;

    const SSL_METHOD *method = TLS_client_method();
    if (method == NULL) return -1;

    g_ssl_ctx = SSL_CTX_new(method);
    if (g_ssl_ctx == NULL) return -1;

    /* Use system default certificate store */
    SSL_CTX_set_default_verify_paths(g_ssl_ctx);

    /* Enable hostname verification */
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_PEER, NULL);

    /* Set minimum TLS version to 1.2 */
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_2_VERSION);

    return 0;
}

/* ===== Public API ===== */

/*
 * Initialize the socket subsystem. Call once at program startup.
 * Initializes OpenSSL and the TLS handle table.
 */
void sf_socket_init(void) {
    /* OpenSSL >= 1.1.0 auto-initializes, but explicit init is harmless */
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    tls_table_init();
}

/*
 * Connect to host:port via TCP using non-blocking connect.
 * Performs DNS resolution via getaddrinfo, tries all returned addresses.
 * Sets the socket to non-blocking mode before returning.
 *
 * Returns: fd on success, -1 on error.
 */
int64_t sf_tcp_connect(const char *host, int64_t port) {
    if (host == NULL) return -1;
    if (port < 0 || port > 65535) return -1;

    /* Convert port to string for getaddrinfo */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP */
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *result = NULL;
    int gai_err = getaddrinfo(host, port_str, &hints, &result);
    if (gai_err != 0 || result == NULL) {
        return -1;
    }

    int fd = -1;
    struct addrinfo *rp;

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) continue;

        /* Set non-blocking before connect for async connect */
        if (set_nonblocking(fd) != 0) {
            close(fd);
            fd = -1;
            continue;
        }

#ifdef __APPLE__
        /* Prevent SIGPIPE on write to closed socket */
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif

        int ret = connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (ret == 0) {
            /* Connected immediately (unlikely for non-blocking, but possible on loopback) */
            break;
        }

        if (errno == EINPROGRESS || errno == EINTR) {
            /* Wait for connection to complete with a reasonable timeout (10s) */
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;

            int poll_ret = poll(&pfd, 1, 10000);
            if (poll_ret > 0 && (pfd.revents & POLLOUT)) {
                /* Check if connect succeeded */
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    break;  /* Connected successfully */
                }
            }
            /* Connection failed on this address, try next */
            close(fd);
            fd = -1;
        } else {
            /* Immediate error, try next address */
            close(fd);
            fd = -1;
        }
    }

    freeaddrinfo(result);
    return (int64_t)fd;
}

/*
 * Poll a socket for readiness.
 *
 * events: 0 = poll for readability, 1 = poll for writability
 * timeout_ms: milliseconds to wait (0 = non-blocking check, -1 = infinite)
 *
 * Returns: 1 = ready, 0 = timeout, -1 = error
 */
int64_t sf_tcp_poll(int64_t fd, int64_t events, int64_t timeout_ms) {
    if (fd < 0) return -1;

    struct pollfd pfd;
    pfd.fd = (int)fd;
    pfd.events = (events == 0) ? (POLLIN | POLLHUP) : POLLOUT;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, (int)timeout_ms);
    if (ret < 0) {
        if (errno == EINTR) return 0;  /* Interrupted, treat as timeout */
        return -1;
    }
    if (ret == 0) return 0;  /* Timeout */

    /* Check for errors */
    if (pfd.revents & (POLLERR | POLLNVAL)) return -1;

    return 1;  /* Ready */
}

/*
 * Read up to len bytes from a non-blocking socket.
 *
 * Returns:
 *   > 0  : bytes read
 *   0    : connection closed by peer (EOF)
 *   -1   : would block (EAGAIN/EWOULDBLOCK) — no data available yet
 *   -2   : error
 */
int64_t sf_tcp_read(int64_t fd, char *buf, int64_t len) {
    if (fd < 0 || buf == NULL || len <= 0) return -2;

    ssize_t n = read((int)fd, buf, (size_t)len);
    if (n > 0) return (int64_t)n;
    if (n == 0) return 0;  /* EOF */

    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    if (errno == EINTR) return -1;  /* Treat interrupt as would_block */
    return -2;
}

/*
 * Write up to len bytes to a non-blocking socket.
 *
 * Returns:
 *   > 0  : bytes written
 *   -1   : would block (EAGAIN/EWOULDBLOCK) — send buffer full
 *   -2   : error (e.g., broken pipe)
 */
int64_t sf_tcp_write(int64_t fd, const char *buf, int64_t len) {
    if (fd < 0 || buf == NULL || len <= 0) return -2;

    int flags = 0;
#ifdef __linux__
    flags = MSG_NOSIGNAL;  /* Prevent SIGPIPE on Linux */
#endif

    ssize_t n = send((int)fd, buf, (size_t)len, flags);
    if (n >= 0) return (int64_t)n;

    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    if (errno == EINTR) return -1;  /* Treat interrupt as would_block */
    return -2;
}

/*
 * Close a TCP socket and release the file descriptor.
 */
void sf_tcp_close(int64_t fd) {
    if (fd >= 0) {
        close((int)fd);
    }
}

/*
 * Perform a TLS handshake on an existing connected socket.
 * Uses SNI (Server Name Indication) with the provided hostname.
 *
 * Returns: TLS handle (>0) on success, -1 on error.
 * The handle is an index into the internal TLS table.
 */
int64_t sf_tls_connect(int64_t fd, const char *hostname) {
    if (fd < 0 || hostname == NULL) return -1;

    if (ensure_ssl_ctx() != 0) return -1;

    SSL *ssl = SSL_new(g_ssl_ctx);
    if (ssl == NULL) return -1;

    /* Attach the socket fd to the SSL object */
    if (SSL_set_fd(ssl, (int)fd) != 1) {
        SSL_free(ssl);
        return -1;
    }

    /* Enable SNI */
    SSL_set_tlsext_host_name(ssl, hostname);

    /* Enable hostname verification */
    SSL_set1_host(ssl, hostname);

    /*
     * Perform the TLS handshake. Since the socket is non-blocking,
     * we need to handle SSL_ERROR_WANT_READ/WRITE by polling.
     */
    while (1) {
        int ret = SSL_connect(ssl);
        if (ret == 1) {
            /* Handshake complete */
            break;
        }

        int ssl_err = SSL_get_error(ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            struct pollfd pfd = { .fd = (int)fd, .events = POLLIN, .revents = 0 };
            int poll_ret = poll(&pfd, 1, 10000);
            if (poll_ret <= 0) {
                SSL_free(ssl);
                return -1;
            }
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            struct pollfd pfd = { .fd = (int)fd, .events = POLLOUT, .revents = 0 };
            int poll_ret = poll(&pfd, 1, 10000);
            if (poll_ret <= 0) {
                SSL_free(ssl);
                return -1;
            }
        } else {
            /* Fatal error */
            SSL_free(ssl);
            return -1;
        }
    }

    /* Store the SSL* in our handle table */
    int64_t handle = tls_table_alloc(ssl);
    if (handle < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }

    return handle;
}

/*
 * Read up to len bytes through a TLS connection.
 *
 * Returns:
 *   > 0  : bytes read (decrypted)
 *   0    : connection closed (TLS shutdown received)
 *   -1   : would block (need more data from network)
 *   -2   : error
 */
int64_t sf_tls_read(int64_t tls_handle, char *buf, int64_t len) {
    if (buf == NULL || len <= 0) return -2;

    SSL *ssl = tls_table_get(tls_handle);
    if (ssl == NULL) return -2;

    int n = SSL_read(ssl, buf, (int)len);
    if (n > 0) return (int64_t)n;

    int ssl_err = SSL_get_error(ssl, n);
    switch (ssl_err) {
        case SSL_ERROR_ZERO_RETURN:
            return 0;  /* Clean TLS shutdown */
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            return -1;  /* Would block */
        default:
            return -2;  /* Error */
    }
}

/*
 * Write up to len bytes through a TLS connection.
 *
 * Returns:
 *   > 0  : bytes written (before encryption)
 *   -1   : would block (send buffer full)
 *   -2   : error
 */
int64_t sf_tls_write(int64_t tls_handle, const char *buf, int64_t len) {
    if (buf == NULL || len <= 0) return -2;

    SSL *ssl = tls_table_get(tls_handle);
    if (ssl == NULL) return -2;

    int n = SSL_write(ssl, buf, (int)len);
    if (n > 0) return (int64_t)n;

    int ssl_err = SSL_get_error(ssl, n);
    switch (ssl_err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            return -1;  /* Would block */
        default:
            return -2;  /* Error */
    }
}

/*
 * Close a TLS connection: performs SSL_shutdown, frees the SSL object,
 * and releases the handle table slot.
 *
 * Note: This does NOT close the underlying socket fd. The caller should
 * call sf_tcp_close() separately after sf_tls_close().
 */
void sf_tls_close(int64_t tls_handle) {
    SSL *ssl = tls_table_get(tls_handle);
    if (ssl == NULL) return;

    /*
     * SSL_shutdown may need to be called twice for a clean bidirectional
     * shutdown. We attempt once; if the peer has already closed, that's fine.
     */
    int ret = SSL_shutdown(ssl);
    if (ret == 0) {
        /* First shutdown sent our close_notify; try to read peer's */
        SSL_shutdown(ssl);
    }

    SSL_free(ssl);
    tls_table_free(tls_handle);
}

/* ===== TCP Server API ===== */

/*
 * Create a TCP socket, set SO_REUSEADDR, bind to host:port, and set non-blocking.
 * Use host "0.0.0.0" to bind on all interfaces.
 *
 * Returns: fd on success, -1 on error.
 */
int64_t sf_tcp_bind(const char *host, int64_t port) {
    if (host == NULL) return -1;
    if (port < 0 || port > 65535) return -1;

    /* Convert port to string for getaddrinfo */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;        /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP */
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;      /* For bind */

    struct addrinfo *result = NULL;
    int gai_err = getaddrinfo(host, port_str, &hints, &result);
    if (gai_err != 0 || result == NULL) {
        return -1;
    }

    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return -1;
    }

    /* Allow address reuse to avoid "address already in use" on restart */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(fd, result->ai_addr, result->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(result);
        return -1;
    }

    freeaddrinfo(result);

    if (set_nonblocking(fd) != 0) {
        close(fd);
        return -1;
    }

    return (int64_t)fd;
}

/*
 * Start listening on a bound TCP socket with the given backlog.
 *
 * Returns: 0 on success, -1 on error.
 */
int64_t sf_tcp_listen(int64_t fd, int64_t backlog) {
    if (fd < 0) return -1;
    if (backlog <= 0) backlog = 128;  /* Sensible default */

    int ret = listen((int)fd, (int)backlog);
    return (ret == 0) ? 0 : -1;
}

/*
 * Non-blocking accept on a listening TCP socket.
 * If a connection is pending, accepts it, sets the new fd to non-blocking
 * (with SO_NOSIGPIPE on macOS), and returns the new fd.
 *
 * Returns:
 *   >= 0 : new client fd (accepted successfully)
 *   -1   : would block (no pending connection)
 *   -2   : error
 */
int64_t sf_tcp_accept(int64_t fd) {
    if (fd < 0) return -2;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = accept((int)fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd >= 0) {
        /* Set the new client socket to non-blocking */
        if (set_nonblocking(client_fd) != 0) {
            close(client_fd);
            return -2;
        }

#ifdef __APPLE__
        /* Prevent SIGPIPE on write to closed socket */
        int opt = 1;
        setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif

        return (int64_t)client_fd;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    if (errno == EINTR) return -1;  /* Treat interrupt as would_block */
    return -2;
}

/* ===== UDP Socket API ===== */

/*
 * Create a non-blocking UDP socket (IPv4).
 *
 * Returns: fd on success, -1 on error.
 */
int64_t sf_udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return -1;

    if (set_nonblocking(fd) != 0) {
        close(fd);
        return -1;
    }

    return (int64_t)fd;
}

/*
 * Send data via UDP to a specific host:port.
 * Performs DNS resolution on the destination host (typically an IP string like "8.8.8.8").
 *
 * Returns: bytes sent on success, -1 for would_block, -2 for error.
 */
int64_t sf_udp_sendto(int64_t fd, const char *buf, int64_t len, const char *host, int64_t port) {
    if (fd < 0 || buf == NULL || len <= 0 || host == NULL) return -2;
    if (port < 0 || port > 65535) return -2;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    /* Convert IP string to binary. For DNS we always send to an IP address. */
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        /* If not a valid IP, try getaddrinfo as fallback */
        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%d", (int)port);
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo *result = NULL;
        if (getaddrinfo(host, port_str, &hints, &result) != 0 || result == NULL) {
            return -2;
        }
        memcpy(&addr, result->ai_addr, sizeof(addr));
        freeaddrinfo(result);
    }

    ssize_t n = sendto((int)fd, buf, (size_t)len, 0,
                       (struct sockaddr *)&addr, sizeof(addr));
    if (n >= 0) return (int64_t)n;

    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    if (errno == EINTR) return -1;
    return -2;
}

/*
 * Receive data from a UDP socket.
 *
 * Returns:
 *   > 0  : bytes received
 *   -1   : would block (no data available yet)
 *   -2   : error
 */
int64_t sf_udp_recvfrom(int64_t fd, char *buf, int64_t len) {
    if (fd < 0 || buf == NULL || len <= 0) return -2;

    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    ssize_t n = recvfrom((int)fd, buf, (size_t)len, 0,
                         (struct sockaddr *)&sender_addr, &addr_len);
    if (n >= 0) return (int64_t)n;

    if (errno == EAGAIN || errno == EWOULDBLOCK) return -1;
    if (errno == EINTR) return -1;
    return -2;
}

/*
 * Close a UDP socket.
 */
void sf_udp_close(int64_t fd) {
    if (fd >= 0) {
        close((int)fd);
    }
}

/* ===== Server-side TLS ===== */

/*
 * Table of server SSL_CTX pointers (supports multiple cert/key configurations).
 * Indices are 1-based (0 means "no context").
 */
#define SF_MAX_TLS_CTX 16
static SSL_CTX *ctx_table[SF_MAX_TLS_CTX];
static int ctx_table_initialized = 0;

static void ctx_table_init(void) {
    if (!ctx_table_initialized) {
        memset(ctx_table, 0, sizeof(ctx_table));
        ctx_table_initialized = 1;
    }
}

/*
 * Create a new server SSL_CTX loaded with the given certificate and private key.
 *
 * cert_path: path to PEM certificate file (or chain)
 * key_path:  path to PEM private key file
 *
 * Returns: context handle (>0) on success, -1 on error.
 */
int64_t sf_tls_ctx_new_server(const char *cert_path, const char *key_path) {
    if (cert_path == NULL || key_path == NULL) return -1;

    ctx_table_init();

    const SSL_METHOD *method = TLS_server_method();
    if (method == NULL) return -1;

    SSL_CTX *ctx = SSL_CTX_new(method);
    if (ctx == NULL) return -1;

    /* Set minimum TLS version to 1.2 */
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    /* Load certificate */
    if (SSL_CTX_use_certificate_chain_file(ctx, cert_path) != 1) {
        SSL_CTX_free(ctx);
        return -1;
    }

    /* Load private key */
    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(ctx);
        return -1;
    }

    /* Verify key matches certificate */
    if (SSL_CTX_check_private_key(ctx) != 1) {
        SSL_CTX_free(ctx);
        return -1;
    }

    /* Allocate a slot in the context table */
    for (int i = 0; i < SF_MAX_TLS_CTX; i++) {
        if (ctx_table[i] == NULL) {
            ctx_table[i] = ctx;
            return (int64_t)(i + 1);
        }
    }

    /* Table full */
    SSL_CTX_free(ctx);
    return -1;
}

/*
 * Create a new client SSL_CTX with custom settings.
 *
 * ca_path:     path to CA certificate file (NULL for system default)
 * verify_peer: 1 = verify server cert, 0 = skip verification
 *
 * Returns: context handle (>0) on success, -1 on error.
 */
int64_t sf_tls_ctx_new_client(const char *ca_path, int64_t verify_peer) {
    ctx_table_init();

    const SSL_METHOD *method = TLS_client_method();
    if (method == NULL) return -1;

    SSL_CTX *ctx = SSL_CTX_new(method);
    if (ctx == NULL) return -1;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    if (ca_path != NULL && strlen(ca_path) > 0) {
        if (SSL_CTX_load_verify_locations(ctx, ca_path, NULL) != 1) {
            SSL_CTX_free(ctx);
            return -1;
        }
    } else {
        SSL_CTX_set_default_verify_paths(ctx);
    }

    if (verify_peer) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }

    for (int i = 0; i < SF_MAX_TLS_CTX; i++) {
        if (ctx_table[i] == NULL) {
            ctx_table[i] = ctx;
            return (int64_t)(i + 1);
        }
    }

    SSL_CTX_free(ctx);
    return -1;
}

/*
 * Perform a TLS handshake as SERVER on an accepted client fd.
 * Uses the given server context (from sf_tls_ctx_new_server).
 *
 * Returns: TLS handle (>0) on success, -1 on error.
 */
int64_t sf_tls_accept(int64_t client_fd, int64_t ctx_handle) {
    if (client_fd < 0) return -1;
    if (ctx_handle < 1 || ctx_handle > SF_MAX_TLS_CTX) return -1;

    SSL_CTX *ctx = ctx_table[ctx_handle - 1];
    if (ctx == NULL) return -1;

    SSL *ssl = SSL_new(ctx);
    if (ssl == NULL) return -1;

    if (SSL_set_fd(ssl, (int)client_fd) != 1) {
        SSL_free(ssl);
        return -1;
    }

    /* Non-blocking TLS accept with poll loop */
    while (1) {
        int ret = SSL_accept(ssl);
        if (ret == 1) {
            break;  /* Handshake complete */
        }

        int ssl_err = SSL_get_error(ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            struct pollfd pfd = { .fd = (int)client_fd, .events = POLLIN, .revents = 0 };
            int poll_ret = poll(&pfd, 1, 10000);  /* 10s timeout */
            if (poll_ret <= 0) {
                SSL_free(ssl);
                return -1;
            }
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            struct pollfd pfd = { .fd = (int)client_fd, .events = POLLOUT, .revents = 0 };
            int poll_ret = poll(&pfd, 1, 10000);
            if (poll_ret <= 0) {
                SSL_free(ssl);
                return -1;
            }
        } else {
            /* Fatal handshake error */
            SSL_free(ssl);
            return -1;
        }
    }

    /* Store in handle table */
    int64_t handle = tls_table_alloc(ssl);
    if (handle < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }

    return handle;
}

/*
 * Perform a TLS client handshake using a specific context (not the global one).
 * Useful for custom CA certs or skip-verification.
 *
 * Returns: TLS handle (>0) on success, -1 on error.
 */
int64_t sf_tls_connect_ctx(int64_t fd, const char *hostname, int64_t ctx_handle) {
    if (fd < 0 || hostname == NULL) return -1;
    if (ctx_handle < 1 || ctx_handle > SF_MAX_TLS_CTX) return -1;

    SSL_CTX *ctx = ctx_table[ctx_handle - 1];
    if (ctx == NULL) return -1;

    SSL *ssl = SSL_new(ctx);
    if (ssl == NULL) return -1;

    if (SSL_set_fd(ssl, (int)fd) != 1) {
        SSL_free(ssl);
        return -1;
    }

    SSL_set_tlsext_host_name(ssl, hostname);
    SSL_set1_host(ssl, hostname);

    while (1) {
        int ret = SSL_connect(ssl);
        if (ret == 1) break;

        int ssl_err = SSL_get_error(ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            struct pollfd pfd = { .fd = (int)fd, .events = POLLIN, .revents = 0 };
            if (poll(&pfd, 1, 10000) <= 0) { SSL_free(ssl); return -1; }
        } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
            struct pollfd pfd = { .fd = (int)fd, .events = POLLOUT, .revents = 0 };
            if (poll(&pfd, 1, 10000) <= 0) { SSL_free(ssl); return -1; }
        } else {
            SSL_free(ssl);
            return -1;
        }
    }

    int64_t handle = tls_table_alloc(ssl);
    if (handle < 0) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        return -1;
    }

    return handle;
}

/*
 * Free a TLS context and release its slot in the context table.
 */
void sf_tls_ctx_free(int64_t ctx_handle) {
    if (ctx_handle < 1 || ctx_handle > SF_MAX_TLS_CTX) return;

    SSL_CTX *ctx = ctx_table[ctx_handle - 1];
    if (ctx != NULL) {
        SSL_CTX_free(ctx);
        ctx_table[ctx_handle - 1] = NULL;
    }
}
