# @ssl — Optional TLS/SSL Module

Server-side TLS for HTTPS, extending the existing client TLS support in the socket layer. Optional module — requires OpenSSL (`libssl` + `libcrypto`). Enables Saffron services to terminate TLS natively without a reverse proxy.

---

## API

### Context Creation

```saffron
import "@ssl" as SSL

// Server: load certificate and private key
var ctx = SSL.server_context("certs/server.crt", "certs/server.key")

// Client: default verification (system CA bundle)
var ctx = SSL.client_context()

// Client: custom CA
var ctx = SSL.client_context({ca: "certs/custom-ca.pem"})

// Client: skip verification (dev only)
var ctx = SSL.client_context({verify: false})
```

### Wrapping Connections

```saffron
// Server-side: wrap an accepted socket
var tls_conn = SSL.wrap_server(ctx, client_socket)

// Client-side: wrap a connected socket
var tls_conn = SSL.wrap_client(ctx, socket, "example.com")  // SNI hostname
```

### Reading and Writing

```saffron
SSL.write(tls_conn, "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello")
var data = SSL.read(tls_conn, 4096)
SSL.close(tls_conn)  // sends TLS close_notify, then closes underlying socket
```

### Self-Signed Certificate Generation

```saffron
// Generates cert + key files (shells out to openssl CLI)
SSL.generate_self_signed({
    cn: "localhost",
    days: 365,
    cert_path: "certs/dev.crt",
    key_path: "certs/dev.key"
})
```

### Constants

```saffron
SSL.TLS_1_2    // minimum TLS 1.2
SSL.TLS_1_3    // require TLS 1.3 only

// Usage: restrict protocol version
var ctx = SSL.server_context("server.crt", "server.key", {min_version: SSL.TLS_1_3})
```

---

## HTTP Server Integration

The `@http/server` module accepts an optional TLS context to serve HTTPS:

```saffron
import "@http/server" as Http
import "@ssl" as SSL

var tls = SSL.server_context("certs/server.crt", "certs/server.key")

var server = Http.Server({port: 443, tls: tls})

server.get("/", fun (req: Http.Request): Http.Response {
    return Http.Response(200, "Hello, HTTPS!")
})

server.listen()
IO.println("HTTPS server running on :443")
```

Under the hood:
1. `Http.Server` calls `accept()` on the TCP socket as normal
2. When `tls` option is set, immediately calls `SSL.wrap_server(ctx, conn)` on each accepted connection
3. All subsequent read/write on that connection goes through `SSL.read`/`SSL.write`
4. On connection close, `SSL.close` sends `close_notify` before TCP teardown

### Dual HTTP + HTTPS

```saffron
var tls = SSL.server_context("certs/server.crt", "certs/server.key")

var http_server = Http.Server({port: 80})
var https_server = Http.Server({port: 443, tls: tls})

// Redirect HTTP to HTTPS
http_server.get("*", fun (req: Http.Request): Http.Response {
    return Http.Response(301, "", {"Location": "https://${req.host()}${req.path()}"})
})

https_server.get("/api/health", fun (req: Http.Request): Http.Response {
    return Http.Response(200, Json.encode({"status": "ok"}))
})

// Run both (async)
Task.spawn(fun () => http_server.listen())
https_server.listen()
```

---

## HTTP Client Integration

Extend the existing `@http` client to support custom TLS settings:

```saffron
import "@http" as Http
import "@ssl" as SSL

// Default: uses system CAs, verifies hostname (already works today)
var resp = Http.get("https://api.example.com/data")

// Custom CA (corporate environments)
var ctx = SSL.client_context({ca: "/etc/corp/ca-bundle.pem"})
var resp = Http.get("https://internal.corp.com/api", {tls: ctx})

// Skip verification (dev/testing only)
var ctx = SSL.client_context({verify: false})
var resp = Http.get("https://localhost:8443/test", {tls: ctx})
```

---

## C Runtime Extensions

New functions in `cvm/libc/socket_native.c` (or a new `tls_native.c`):

```c
// Create server TLS context, load cert+key
// Returns opaque SSL_CTX* as tagged pointer
Value sf_tls_ctx_new_server(const char* cert_path, const char* key_path, int min_version);

// Create client TLS context
Value sf_tls_ctx_new_client(const char* ca_path, int verify, int min_version);

// Wrap accepted socket fd into TLS connection
// Performs SSL_accept handshake, returns opaque SSL* as tagged pointer
Value sf_tls_accept(Value ctx, int socket_fd);

// Wrap connected socket fd into TLS client connection
// Performs SSL_connect handshake with SNI
Value sf_tls_connect(Value ctx, int socket_fd, const char* hostname);

// Read decrypted bytes from TLS connection
int sf_tls_read(Value ssl, char* buf, int len);

// Write bytes through TLS connection
int sf_tls_write(Value ssl, const char* buf, int len);

// Close TLS connection (close_notify + free SSL object)
void sf_tls_close(Value ssl);

// Free context when no longer needed
void sf_tls_ctx_free(Value ctx);
```

Corresponding `@extern` declarations in the Saffron module:

```saffron
@extern("i8* sf_tls_ctx_new_server(i8*, i8*, i32)")
fun _tls_ctx_new_server(cert: Int, key: Int, min_ver: Int): Int

@extern("i8* sf_tls_ctx_new_client(i8*, i32, i32)")
fun _tls_ctx_new_client(ca: Int, verify: Int, min_ver: Int): Int

@extern("i8* sf_tls_accept(i8*, i32)")
fun _tls_accept(ctx: Int, fd: Int): Int

@extern("i8* sf_tls_connect(i8*, i32, i8*)")
fun _tls_connect(ctx: Int, fd: Int, hostname: Int): Int

@extern("i32 sf_tls_read(i8*, i8*, i32)")
fun _tls_read(ssl: Int, buf: Int, len: Int): Int

@extern("i32 sf_tls_write(i8*, i8*, i32)")
fun _tls_write(ssl: Int, buf: Int, len: Int): Int

@extern("void sf_tls_close(i8*)")
fun _tls_close(ssl: Int): Nil

@extern("void sf_tls_ctx_free(i8*)")
fun _tls_ctx_free(ctx: Int): Nil
```

---

## Optional Dependency

`tools/saffron` already links `-lssl -lcrypto` for the async HTTP client. For builds without OpenSSL:

Detection:
```bash
# In tools/saffron build script
if pkg-config --exists openssl 2>/dev/null; then
    TLS_AVAILABLE=1
    TLS_FLAGS="$(pkg-config --cflags openssl)"
    TLS_LIBS="$(pkg-config --libs openssl)"
elif [ -d "$(brew --prefix openssl 2>/dev/null)" ]; then
    TLS_AVAILABLE=1
    OPENSSL_PREFIX="$(brew --prefix openssl)"
    TLS_FLAGS="-I${OPENSSL_PREFIX}/include"
    TLS_LIBS="-L${OPENSSL_PREFIX}/lib -lssl -lcrypto"
else
    TLS_AVAILABLE=0
fi
```

Build flags:
- `--no-tls` — explicitly disable TLS support; `import "@ssl"` throws a compile error
- Default: TLS enabled when OpenSSL is detected
- `pantry.toml` declaration:

```toml
[system-dependencies]
openssl = { required = false, pkg-config = "openssl", min-version = "1.1.1" }
```

---

## Self-Signed Certificate Generation

For development convenience, `SSL.generate_self_signed()` shells out to the `openssl` CLI:

```bash
openssl req -x509 -newkey rsa:2048 -keyout dev.key -out dev.crt \
    -days 365 -nodes -subj "/CN=localhost" \
    -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
```

This avoids linking the OpenSSL certificate-generation APIs (which are complex). Requires `openssl` CLI on PATH.

---

## Error Handling

Errors throw with consistent format:

```saffron
// "ssl: <operation>: <detail>"
// Examples:
// "ssl: handshake: certificate verify failed"
// "ssl: read: connection reset by peer"
// "ssl: context: unable to load certificate"
// "ssl: connect: hostname mismatch"

try {
    var ctx = SSL.server_context("missing.crt", "missing.key")
} catch (e) {
    IO.println(e)  // "ssl: context: unable to load certificate"
}
```

---

## Bazaar Usage: HTTPS API Server

The Bazaar registry serves its API over HTTPS in production:

```saffron
import "@http/server" as Http
import "@ssl" as SSL
import "@sqlite" as Sqlite

// Load production certs (Let's Encrypt or similar)
var tls = SSL.server_context(
    "/etc/letsencrypt/live/bazaar.saffron-lang.org/fullchain.pem",
    "/etc/letsencrypt/live/bazaar.saffron-lang.org/privkey.pem",
    {min_version: SSL.TLS_1_2}
)

var server = Http.Server({port: 443, tls: tls})
var db = Sqlite.open("/var/bazaar/registry.db")

server.put("/api/v1/packages/new", fun (req: Http.Request): Http.Response {
    var token = req.header("Authorization").replace("Bearer ", "")
    var user = authenticate(db, token)
    if (user == nil) {
        return Http.Response(401, Json.encode({"error": "unauthorized"}))
    }
    // ... publish logic
    return Http.Response(200, Json.encode({"ok": true}))
})

server.listen()
IO.println("Bazaar HTTPS on :443")
```

---

## Implementation Phases

### Phase 1: Server TLS in C Runtime

- Implement `sf_tls_ctx_new_server`, `sf_tls_accept`, `sf_tls_read`, `sf_tls_write`, `sf_tls_close` in `tls_native.c`
- Add to clang link line in `tools/saffron`
- Test: raw TLS echo server accepting connections from `curl`

### Phase 2: @ssl Saffron Module

- Create `src/lib/ssl.sf` with public API wrapping the C functions
- Context creation, wrap_server, wrap_client, read, write, close
- Constants (TLS_1_2, TLS_1_3)
- `generate_self_signed()` via process spawn
- Error handling with consistent throw format

### Phase 3: HTTP Server Integration

- Modify `@http/server` to accept `tls` option
- Auto-wrap accepted connections when TLS context is provided
- Dual-port (HTTP + HTTPS) example in docs

### Phase 4: HTTP Client Enhancement

- Add `tls` option to `Http.get/post/...`
- Custom CA bundle support
- Skip-verify for development
- Client certificate authentication (mTLS)

### Phase 5: Optional Linking

- `pkg-config` / Homebrew detection in build script
- `--no-tls` flag to compile without OpenSSL
- Compile-time error when `import "@ssl"` is used without TLS support
- `[system-dependencies]` pantry.toml integration
