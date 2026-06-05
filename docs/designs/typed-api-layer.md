# Design: Typed API Layer (Saffron tRPC)

## Summary

A backend-first typed API system where route handlers declare their input/output types, and a codegen tool generates a typed frontend client with auto-caching query primitives. The backend is the source of truth — the frontend conforms to it.

## Goals

1. **Type equivalence**: frontend and backend agree on request/response shapes at compile time
2. **Auto-caching queries**: RTK Query-style cache-by-endpoint with signal-based reactivity
3. **Generated client**: zero manual fetch code — run a tool, get a typed API module
4. **Minimal ceremony**: decorators on existing route handlers, not a separate schema language

## Non-goals

- GraphQL / schema-first design (we're REST-shaped, backend-first)
- Real-time subscriptions (future work, not v1)
- Server-side rendering (turmeric is client-only)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ Backend (native binary)                                     │
│                                                             │
│  @api("/packages/:name", method="GET")                      │
│  fun get_package(name: String): PackageResponse { ... }     │
│                                                             │
│  class PackageResponse { var name: String; ... }            │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          │  saffron api-gen src/routes.sf
                          │     ↓ reads @api decorators + return types
                          │     ↓ emits typed client module
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ Generated: frontend/src/api.sf                              │
│                                                             │
│  import "turmeric/query" as Q                               │
│                                                             │
│  class PackageResponse { var name: String; ... }            │
│                                                             │
│  fun getPackage(name: String): Q.Query<PackageResponse> {   │
│      return Q.query("/api/v1/packages/${name}")             │
│  }                                                         │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          │  import "./api.sf" as Api
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ Frontend (wasm32, turmeric app)                             │
│                                                             │
│  var pkg = Api.getPackage("saffron")                        │
│  pkg.data.get()       // Signal<PackageResponse?>           │
│  pkg.isLoading.get()  // Signal<Bool>                       │
│  pkg.refetch()        // manually re-fetch                  │
└─────────────────────────────────────────────────────────────┘
```

---

## Part 1: Backend Route Decorators

### `@api` decorator

Applied to route handler functions. Declares the HTTP contract:

```saffron
import "@http/server" as Http

/// List all published packages with pagination.
@api("/api/v1/packages", method="GET")
fun list_packages(req: Http.Request): ListPackagesResponse {
    var packages = Db.list_packages()
    return ListPackagesResponse(packages, packages.length())
}

/// Get a single package by name.
@api("/api/v1/packages/:name", method="GET")
fun get_package(req: Http.Request): PackageResponse {
    var name = req.param("name")
    // ...
}

/// Publish a new package version. Requires auth.
@api("/api/v1/packages/publish", method="POST", auth=true)
fun publish_package(req: Http.Request): PublishResponse {
    // ...
}
```

### Response types

Plain classes — no special base class needed:

```saffron
class ListPackagesResponse {
    var packages: List<PackageSummary>
    var total: Int
}

class PackageSummary {
    var name: String
    var description: String
    var latest: String
    var total_downloads: Int
}

class PackageResponse {
    var name: String
    var description: String
    var versions: List<VersionInfo>
}

class VersionInfo {
    var vers: String
    var cksum: String
    var yanked: Bool
    var published_by: String
    var published_at: String
}

class PublishResponse {
    var ok: Bool
    var package: String
    var version: String
}

class ErrorResponse {
    var error: String
}
```

### Request body types (for POST/PUT)

```saffron
class PublishRequest {
    var name: String
    var vers: String
    var tarball: String
    var description: String
    var repository: String
    var license: String
}

@api("/api/v1/packages/publish", method="POST", body=PublishRequest, auth=true)
fun publish_package(req: Http.Request): PublishResponse {
    var body: PublishRequest = req.typed_body()  // auto-parsed from JSON
    // ...
}
```

---

## Part 2: `turmeric/query` — Caching Query Layer

### Core types

```saffron
// turmeric/query.sf

import "turmeric/signal" as Sig

enum QueryStatus {
    Idle,
    Loading,
    Success(data: Any),
    Error(message: String)
}

class Query<T> {
    var data: Sig.Signal<T>           // nil until first success
    var isLoading: Sig.Signal<Bool>
    var error: Sig.Signal<String>
    var status: Sig.Signal<QueryStatus>

    fun refetch()                     // force re-fetch from server
    fun invalidate()                  // mark stale, refetch on next access
}

class Mutation<TReq, TRes> {
    var data: Sig.Signal<TRes>
    var isLoading: Sig.Signal<Bool>
    var error: Sig.Signal<String>

    fun mutate(body: TReq)            // trigger the mutation
    fun reset()                       // clear state
}
```

### Cache behavior

```saffron
// Cache key = endpoint path (with params substituted)
// e.g., "/api/v1/packages/saffron" is one cache entry

var config = Q.Config(
    stale_time=30.0,     // seconds before data is considered stale
    cache_time=300.0,    // seconds before unused cache entry is garbage collected
    refetch_on_focus=true,
    refetch_on_reconnect=true
)

// Queries with same cache key share a single fetch:
var pkg1 = Api.getPackage("saffron")  // fetches
var pkg2 = Api.getPackage("saffron")  // returns cached, no new fetch
```

### Invalidation

```saffron
// After a mutation, invalidate related queries:
var publish = Api.publishPackage()

publish.onSuccess(fun (res: PublishResponse) => {
    Api.listPackages().invalidate()
    Api.getPackage(res.package).invalidate()
})
```

### Factory functions

```saffron
// For GET queries (cached, auto-refetch):
fun query<T>(url: String): Query<T> { ... }
fun query<T>(url: String, config: Config): Query<T> { ... }

// For POST/PUT/DELETE mutations (not cached):
fun mutation<TReq, TRes>(url: String, method: String): Mutation<TReq, TRes> { ... }
```

---

## Part 3: `saffron api-gen` — Client Generator

### Usage

```bash
saffron api-gen src/routes.sf -o frontend/src/api.sf
# or via pantry script:
# [scripts]
# api = "saffron api-gen src/routes.sf -o frontend/src/api.sf"
```

### What it generates

Given backend routes with `@api` decorators, the tool emits:

```saffron
// AUTO-GENERATED — do not edit
// Source: src/routes.sf
// Regenerate: saffron api-gen src/routes.sf -o frontend/src/api.sf

import "turmeric/query" as Q

// --- Types ---

class PackageSummary {
    var name: String
    var description: String
    var latest: String
    var total_downloads: Int
}

class ListPackagesResponse {
    var packages: List<PackageSummary>
    var total: Int
}

// ... all response/request types ...

// --- Queries ---

fun listPackages(): Q.Query<ListPackagesResponse> {
    return Q.query("/api/v1/packages")
}

fun getPackage(name: String): Q.Query<PackageResponse> {
    return Q.query("/api/v1/packages/${name}")
}

fun searchPackages(q: String): Q.Query<ListPackagesResponse> {
    return Q.query("/api/v1/search?q=${q}")
}

// --- Mutations ---

fun publishPackage(): Q.Mutation<PublishRequest, PublishResponse> {
    return Q.mutation("/api/v1/packages/publish", "POST")
}

fun yankPackage(name: String, version: String): Q.Mutation<Nil, YankResponse> {
    return Q.mutation("/api/v1/packages/${name}/yank/${version}", "POST")
}
```

### Generator logic

1. Parse the backend .sf file(s) — find all `@api(...)` decorated functions
2. Extract: path, method, return type, body type (if POST), auth requirement
3. Resolve all referenced classes (follow imports if needed)
4. Emit the client module with:
   - Duplicated type definitions (classes)
   - Query factory calls for GET endpoints
   - Mutation factory calls for POST/PUT/DELETE endpoints
   - Path parameter interpolation from function arguments

### Naming conventions

| Backend | Generated client |
|---------|-----------------|
| `fun list_packages(...)` | `fun listPackages(...)` |
| `fun get_package(...)` | `fun getPackage(...)` |
| `fun publish_package(...)` | `fun publishPackage(...)` |
| Snake case → camelCase for the frontend |

---

## Part 4: Frontend Usage

### Full example

```saffron
import "turmeric/signal" as Sig
import "turmeric/router" as Router
import { div, h1, p, input, text, mount } from "turmeric/prelude"
import { Link } from "turmeric/router"
import "./api.sf" as Api

var router = Router.hash()
var searchQuery = Sig.signal("")

fun SearchBar() {
    var query = Api.searchPackages(searchQuery.get())

    <form on_submit={fun (e: Event) => query.refetch()}>
        <input
            placeholder="Search packages..."
            on_input={fun (e: Event) => searchQuery.set(e.target_value)}
        />
    </form>

    {if (query.isLoading.get()) {
        <p>Searching...</p>
    }}

    {var results = query.data.get()
    if (results != nil) {
        var pkgs = results.packages
        var i: Float = 0
        while (i < pkgs.length()) {
            PackageCard(pkgs[i])
            i = i + 1
        }
    }}
}

fun PackageCard(pkg: Api.PackageSummary) {
    <div cls="card">
        Link(router, "/packages/" + pkg.name) {
            <h2>{pkg.name}</h2>
            <span>v{pkg.latest}</span>
        }
        <p>{pkg.description}</p>
    </div>
}
```

---

## Implementation Plan

### Phase 1: `turmeric/query` module
- `Query` class with signals (data, isLoading, error)
- Cache store (Map<String, CacheEntry>)
- `query()` factory: fetch via FFI, parse JSON, update signals
- Stale time + background refetch
- `invalidate()` to mark entries stale

### Phase 2: `@api` decorator support in parser
- Parser recognizes `@api("path", key=value)` as a decorator
- Stores metadata on the FunDecl AST node
- Codegen ignores it (metadata only, no runtime effect on backend)

### Phase 3: `saffron api-gen` tool
- Written in Saffron (runs via `saffron run tools/api_gen.sf`)
- Parses backend source, extracts `@api` metadata + types
- Generates the typed client .sf file
- Integrable into pantry scripts

### Phase 4: Bazaar migration
- Add `@api` decorators to `bazaar/src/routes.sf`
- Generate `bazaar/frontend/src/api.sf`
- Replace manual `fetch_json` calls with `Api.*` queries
- Remove hand-written signal state management

---

## Open Questions

1. **Auth token injection**: Should `turmeric/query` have a global auth config (`Q.setAuth(token)`) that's auto-attached to requests? Or per-query auth?

2. **Error typing**: Should mutations return `Result<T, ErrorResponse>` or use the error signal? RTK Query uses the error field on the query object.

3. **Pagination**: Should paginated queries be a special `PaginatedQuery<T>` with `fetchNextPage()` / `hasNextPage` signals?

4. **WebSocket subscriptions** (future): If we add WS support, should query invalidation happen via push? ("Package X was published" → auto-refetch listPackages)

5. **Offline support** (future): Should the cache persist to localStorage/IndexedDB for offline-first apps?

---

## Comparison

| Feature | RTK Query | tRPC | Saffron API |
|---------|-----------|------|-------------|
| Source of truth | Schema/API | Backend router | Backend `@api` decorators |
| Type sharing | Manual | Runtime (TS) | Codegen (compile-time) |
| Caching | Automatic | Via React Query | Signal-based automatic |
| Code generation | No | No (runtime types) | Yes (`api-gen`) |
| Transport | HTTP | HTTP/WS | HTTP (FFI fetch) |
| Framework coupling | React/Redux | React/Next | Turmeric signals |
| Invalidation | Tags | Mutation hooks | `invalidate()` + `onSuccess` |
