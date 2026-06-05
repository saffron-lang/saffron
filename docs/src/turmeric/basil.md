# Basil

Basil is a typed, auto-caching query client for Turmeric frontend apps. It provides signal-based data fetching with automatic caching, stale-time management, and cache invalidation — similar to RTK Query or TanStack Query, but with compile-time type safety via generics.

## Setup

```toml
# pantry.toml (frontend package)
[dependencies]
turmeric = { path = "../turmeric" }
basil = { path = "../basil" }
```

```saffron
import "basil/query" as Query
import "basil/mutation" as Mutation
```

## Queries (GET requests)

A query fetches data from a URL, caches it, and exposes reactive signals. The type parameter `T` flows through to the `data` signal:

```saffron
class PackageList {
    var packages: List<Package>
    var total: Float
}

var packages = Query.query<PackageList>("/api/v1/packages")

// In a component:
fun PackageList() {
    if (packages.isLoading.get()) {
        <div>Loading...</div>
    }

    var error = packages.error.get()
    if (error.length() > 0) {
        <div>Error: {error}</div>
    }

    var data: PackageList = packages.data.get()
    if (data != nil) {
        // data is PackageList — no casting needed
        var i: Float = 0
        while (i < data.packages.length()) {
            PackageCard(data.packages[i])
            i = i + 1
        }
    }
}
```

### Query signals

| Signal | Type | Description |
|--------|------|-------------|
| `query.data` | `Signal<T?>` | Parsed, typed response (nil until first success) |
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
var packages = Query.query_with<PackageList>("/api/v1/packages", 60.0)
```

## Mutations (POST/PUT/DELETE)

Mutations are for operations that modify server state. They take two type parameters: `TRequest` (the body type) and `TResponse` (the response type):

```saffron
class PublishRequest {
    var name: String
    var version: String
    var tarball: String
}

class PublishResponse {
    var ok: Bool
    var package_name: String
    var published_version: String
}

var publish = Mutation.mutation<PublishRequest, PublishResponse>("/api/v1/packages/publish")

// Trigger the mutation with a typed body — serialized automatically
publish.mutate(PublishRequest("my-package", "1.0.0", encoded_data))
```

### Mutation signals

| Signal | Type | Description |
|--------|------|-------------|
| `mutation.data` | `Signal<TResponse?>` | Typed response from last successful mutation |
| `mutation.isLoading` | `Signal<Bool>` | True while request is in flight |
| `mutation.error` | `Signal<String>` | Error message (empty on success) |

### Cache invalidation

Invalidate related queries after a successful mutation:

```saffron
var publish = Mutation.mutation<PublishRequest, PublishResponse>("/api/v1/packages/publish")
    .invalidates(["/api/v1/packages"])

// After publish succeeds, any query starting with "/api/v1/packages"
// will be marked stale and refetch on next access.
```

### Success callback

The callback receives the typed response:

```saffron
var publish = Mutation.mutation<PublishRequest, PublishResponse>("/api/v1/packages/publish")
    .invalidates(["/api/v1/packages"])
    .on_success(fun (data: PublishResponse) => {
        IO.println("Published: " + data.package_name)
        router.go("/packages/" + data.package_name)
    })
```

### Explicit HTTP method

```saffron
class YankRequest {
    var package_name: String
    var version: String
}

class YankResponse {
    var ok: Bool
}

var yank = Mutation.mutation_method<YankRequest, YankResponse>("/api/v1/yank", "DELETE")
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

The generated client provides named functions per endpoint with full type safety:

```saffron
// Generated: frontend/src/api.sf
import "basil" as Basil

fun getPackages(): Basil.Query<PackageList> {
    return Basil.query<PackageList>("/api/v1/packages")
}

fun getPackage(name: String): Basil.Query<PackageDetail> {
    return Basil.query<PackageDetail>("/api/v1/packages/${name}")
}

fun createPackagesPublish(): Basil.Mutation<PublishRequest, PublishResponse> {
    return Basil.mutation<PublishRequest, PublishResponse>("/api/v1/packages/publish")
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

    var data: PackageList = packages.data.get()
    if (data != nil) {
        var i: Float = 0
        while (i < data.packages.length()) {
            PackageCard(data.packages[i])
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
- The cache layer uses `Any` internally (type erasure at the storage boundary); type safety is enforced at the `Query<T>` / `Mutation<TReq, TRes>` API surface

## Architecture

```
┌─────────────────────────────────────────────┐
│  Component                                  │
│  var pkgs = Query.query<PkgList>("/packages")   │
│  pkgs.data.get() → Signal<PkgList?> read    │
└──────────────┬──────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────┐
│  Cache Store (Any-typed internally)         │
│  key="/packages" → CacheEntry               │
│  { data: Any, timestamp, stale_time }       │
└──────────────┬──────────────────────────────┘
               │ cache miss or stale
               ▼
┌─────────────────────────────────────────────┐
│  Fetch (wasm FFI)                           │
│  js_fetch_json(url, callback_id)            │
│  → browser fetch() → JSON.parse_as<T>()    │
│  → update Signal<T> + cache                 │
└─────────────────────────────────────────────┘
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
