# Basil

Basil is an auto-caching query client for Turmeric frontend apps. It provides signal-based data fetching with automatic caching, stale-time management, and cache invalidation — similar to RTK Query or TanStack Query.

## Setup

```toml
# pantry.toml (frontend package)
[dependencies]
turmeric = { path = "../turmeric" }
basil = { path = "../basil" }
```

```saffron
import "basil/query" as Q
import "basil/mutation" as M
```

## Queries (GET requests)

A query fetches data from a URL, caches it, and exposes reactive signals:

```saffron
var packages = Q.query("/api/v1/packages")

// In a component:
fun PackageList() {
    if (packages.isLoading.get()) {
        <div>Loading...</div>
    }

    var error = packages.error.get()
    if (error.length() > 0) {
        <div>Error: {error}</div>
    }

    var data = packages.data.get()
    if (data != nil) {
        // render packages...
    }
}
```

### Query signals

| Signal | Type | Description |
|--------|------|-------------|
| `query.data` | `Signal<Any>` | Parsed JSON response (nil until first success) |
| `query.isLoading` | `Signal<Bool>` | True while fetch is in flight |
| `query.error` | `Signal<String>` | Error message (empty on success) |

### Query methods

| Method | Description |
|--------|-------------|
| `query.refetch()` | Force re-fetch from server (ignores cache) |
| `query.invalidate()` | Mark cache entry as stale (next access refetches) |

### Custom stale time

```saffron
// Data stays fresh for 60 seconds (default is 30s)
var packages = Q.query_with("/api/v1/packages", 60.0)
```

## Mutations (POST/PUT/DELETE)

Mutations are for operations that modify server state:

```saffron
var publish = M.mutation("/api/v1/packages/publish")

// Trigger the mutation with a JSON body
publish.mutate(JSON.to_string({
    "name": "my-package",
    "vers": "1.0.0",
    "tarball": encoded_data
}))
```

### Mutation signals

| Signal | Type | Description |
|--------|------|-------------|
| `mutation.data` | `Signal<Any>` | Response data from last successful mutation |
| `mutation.isLoading` | `Signal<Bool>` | True while request is in flight |
| `mutation.error` | `Signal<String>` | Error message (empty on success) |

### Cache invalidation

Invalidate related queries after a successful mutation:

```saffron
var publish = M.mutation("/api/v1/packages/publish")
    .invalidates(["/api/v1/packages"])

// After publish succeeds, any query starting with "/api/v1/packages"
// will be marked stale and refetch on next access.
```

### Success callback

```saffron
var publish = M.mutation("/api/v1/packages/publish")
    .invalidates(["/api/v1/packages"])
    .on_success(fun (data: Any) => {
        IO.println("Published: " + data.get("package"))
        router.go("/packages/" + data.get("package"))
    })
```

### Reset

```saffron
publish.reset()  // Clear data, error, loading state
```

## Generated Clients

Instead of writing query URLs manually, use the Parsley generator to create a typed client module:

```bash
saffron run parsley/tools/gen_client.sf api_spec.json frontend/src/api.sf
```

The generated client provides named functions per endpoint:

```saffron
// Generated: frontend/src/api.sf
import "basil" as B

fun getPackages(): B.Query {
    return B.query("/api/v1/packages")
}

fun getPackage(name: String): B.Query {
    return B.query("/api/v1/packages/${name}")
}

fun createPackagesPublish(): B.Mutation {
    return B.mutation("/api/v1/packages/publish")
}
```

Usage in components:

```saffron
import "./api.sf" as Api

fun HomePage() {
    var packages = Api.getPackages()

    if (packages.isLoading.get()) {
        <div cls="loading">Loading...</div>
    }

    var data = packages.data.get()
    if (data != nil) {
        var pkgs = data.get("packages")
        var i: Float = 0
        while (i < pkgs.length()) {
            PackageCard(pkgs[i])
            i = i + 1
        }
    }
}
```

## Cache Behavior

- Queries with the same URL share a single cache entry
- Data is considered **fresh** for `stale_time` seconds (default 30s)
- Stale data is returned immediately while a background refetch happens
- `invalidate()` marks an entry as stale without removing it
- `invalidate_matching(prefix)` invalidates all entries whose URL starts with the prefix
- Mutations never cache their responses

## Architecture

```
┌─────────────────────────────────────┐
│  Component                          │
│  var pkgs = Q.query("/packages")    │
│  pkgs.data.get() → Signal read     │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  Cache Store                        │
│  key="/packages" → CacheEntry       │
│  { data, timestamp, stale_time }    │
└──────────────┬──────────────────────┘
               │ cache miss or stale
               ▼
┌─────────────────────────────────────┐
│  Fetch (wasm FFI)                   │
│  js_fetch_json(url, callback_id)    │
│  → browser fetch() → JSON parse    │
│  → update signal + cache           │
└─────────────────────────────────────┘
```

## FFI Requirements

Basil requires two JS functions in the wasm host environment:

```javascript
// In app_template.js or equivalent:
js_fetch_json: (urlPtr, callbackId) => {
    const url = readCString(urlPtr);
    fetch(url).then(r => r.text()).then(text => {
        const ptr = writeCString(text);
        instance.exports.__on_fetch_complete(callbackId, ptr);
    });
},
js_fetch_post: (urlPtr, bodyPtr, callbackId) => {
    const url = readCString(urlPtr);
    const body = readCString(bodyPtr);
    fetch(url, {method:'POST', headers:{'Content-Type':'application/json'}, body})
        .then(r => r.text()).then(text => {
            const ptr = writeCString(text);
            instance.exports.__on_fetch_complete(callbackId, ptr);
        });
}
```

These are already provided by the turmeric runtime's `app_template.js`.
