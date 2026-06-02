# Log

```saffron
import "@log" as Log
```

Structured logging with levels, text/JSON formatters, and configurable output.

## Log Levels

```saffron
enum Level {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
}
```

## Creating a Logger

```saffron
import "@log" as Log

var logger = Log.Logger("my-app")
```

### From environment variable

```saffron
// Reads LOG_LEVEL env var (e.g., "debug", "info", "warn")
var logger = Log.from_env("LOG_LEVEL")
```

## Logging Messages

```saffron
logger.info("server started on port ${port}")
logger.warn("connection pool running low", {"available": 2})
logger.error("request failed", {"status": 500, "path": "/api/users"})
logger.debug("processing item ${id}")
```

Each log method accepts an optional second argument -- a map of structured fields:

```saffron
logger.info("user login", {"user_id": "42", "ip": "10.0.0.1"})
```

## Output Formats

### Text format (default)

```
2026-06-02T10:30:00 INFO  [my-app] server started on port 8080
```

### JSON format

```json
{"timestamp":"2026-06-02T10:30:00","level":"INFO","logger":"my-app","message":"server started on port 8080"}
```

## Example

```saffron
import "@log" as Log

var log = Log.Logger("web-server")

log.info("starting server", {"port": "8080"})
log.debug("loading config from config.toml")
log.warn("deprecated endpoint called", {"path": "/api/v1/users"})
log.error("database connection failed", {"host": "localhost", "retry": "3"})
```
