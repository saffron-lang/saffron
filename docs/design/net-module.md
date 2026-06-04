# `@net` Module Design

High-level networking module that unifies `@socket`, `@server`, and `@dns` behind
an ergonomic, object-oriented API. All operations integrate with the async scheduler.

## Motivation

Today, TCP clients use `@socket` (raw fd integers), servers use `@server` (returns
`Listener` with int-based `accept`), and DNS lives in `@dns`. Users must manually
track fd lifecycles and know which module handles each operation. `@net` provides a
single import with typed connection objects.

## API Surface

```saffron
import "@net" as Net
```

### TCP Server

```saffron
var listener = Net.listen("0.0.0.0", 8080)
while (true) {
    var conn = listener.accept()       // yields until client connects
    IO.println(conn.remote_addr())     // "192.168.1.5:49320"
    conn.write("Hello!\n")
    var data = conn.read(1024)         // yields until data available
    conn.close()
}
listener.close()
```

### TCP Client

```saffron
var conn = Net.connect("example.com", 80)
conn.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n")
var response = conn.read_all()
conn.close()
```

### TLS

```saffron
var tls = Net.connect_tls("example.com", 443)
tls.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n")
var body = tls.read_all()
tls.close()
```

### UDP

```saffron
var sock = Net.udp_bind("0.0.0.0", 9000)
var [data, addr] = sock.recv_from(1024)   // yields
sock.send_to("response", addr)
sock.close()
```

### IP Address

```saffron
var ip = Net.parse_ip("192.168.1.1")
ip.is_v4()      // true
ip.is_private() // true
ip.to_string()  // "192.168.1.1"
```

### Utilities

```saffron
Net.resolve("example.com")  // ["93.184.216.34"] (delegates to @dns)
Net.local_ip()              // "192.168.1.100"
```

## Types

### TcpListener

Wraps `@server.Listener`. Returned by `Net.listen(host, port)`.

| Method | Description |
|--------|-------------|
| `accept(): TcpConnection` | Yield until a client connects; return wrapped connection |
| `close()` | Stop listening, release the fd |
| `addr(): String` | Bound address as `"host:port"` |

### TcpConnection

Wraps a socket fd (plain or TLS handle). Returned by `accept()`, `connect()`, `connect_tls()`.

| Method | Description |
|--------|-------------|
| `read(max: Int): String` | Read up to `max` bytes; yields if would block |
| `read_all(): String` | Read until EOF (accumulates chunks) |
| `write(data: String): Int` | Write data; yields if would block; returns bytes written |
| `close()` | Close the underlying fd/TLS handle |
| `remote_addr(): String` | Peer address as `"ip:port"` |
| `is_tls(): Bool` | Whether this connection uses TLS |

Internally stores `_fd: Int` and `_tls_handle: Int` (0 if plain). Delegates to
`Socket.read`/`Socket.tls_read` based on `_tls_handle`.

### UdpSocket

New wrapper around the raw UDP externs already in `dns.sf`.

| Method | Description |
|--------|-------------|
| `recv_from(max: Int): List` | Returns `[data, addr_string]`; yields if would block |
| `send_to(data: String, addr: String): Int` | Send datagram to address |
| `close()` | Close the socket |
| `addr(): String` | Bound local address |

### IpAddress

Pure Saffron class -- no FFI needed.

| Method | Description |
|--------|-------------|
| `is_v4(): Bool` | True if IPv4 |
| `is_v6(): Bool` | True if IPv6 |
| `is_private(): Bool` | RFC 1918 / loopback check |
| `is_loopback(): Bool` | 127.0.0.0/8 or ::1 |
| `to_string(): String` | Canonical string form |

## How It Wraps Existing Modules

```
@net (this module)
 |-- TcpListener   --> @server.Listener (bind + listen + accept)
 |-- TcpConnection --> @socket (read/write/close, tls_read/tls_write/tls_close)
 |-- UdpSocket     --> @dns UDP externs (sf_udp_socket, sf_udp_sendto, sf_udp_recvfrom)
 |-- resolve()     --> @dns.resolve() / resolve_all()
 |-- IpAddress     --> pure Saffron (parsing + classification)
```

No new C runtime functions are required for the initial implementation. The UDP
externs (`sf_udp_socket`, `sf_udp_sendto`, `sf_udp_recvfrom`, `sf_udp_close`) are
currently private to `dns.sf` and need to be either re-declared or extracted into a
shared `@udp` internal module.

## Async Integration

All blocking methods (`accept`, `read`, `recv_from`) use `__suspend(reason, fd)` to
yield to the scheduler, identical to the pattern in `@socket` and `@server`. This
means `@net` connections work transparently inside `Task.spawn`:

```saffron
import "@net" as Net

var listener = Net.listen("0.0.0.0", 8080)
while (true) {
    var conn = listener.accept()
    Task.spawn(fun () => handle_client(conn))
}
```

## TLS Support

- **Client**: `Net.connect_tls(host, port)` calls `Socket.connect_tls` internally
  and returns a `TcpConnection` with `_tls_handle > 0`.
- **Server**: `Net.listen_tls(host, port, ssl_ctx)` wraps accepted fds with
  `SSL.wrap_server(fd, ctx)` before returning. Requires an `@ssl.Context`.

## Implementation Plan

1. **Phase 1** -- `TcpConnection` + `TcpListener` + `connect`/`listen` (pure wrappers)
2. **Phase 2** -- `UdpSocket` (extract UDP externs from dns.sf into shared declarations)
3. **Phase 3** -- `IpAddress` (pure Saffron, no FFI)
4. **Phase 4** -- `resolve()`, `local_ip()` utilities
5. **Phase 5** -- `listen_tls` server-side TLS convenience

Each phase is independently shippable. Phase 1 alone covers the most common use case.

## File Location

`src/lib/net.sf` -- imported as `import "@net" as Net`.
