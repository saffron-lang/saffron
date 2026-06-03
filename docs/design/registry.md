# Bazaar — Pantry Package Registry

"Bazaar" — a marketplace themed with the spice motif.

**URL:** `bazaar.saffron-lang.org`

Built entirely with Saffron (backend REST API) and Turmeric (frontend WASM).

---

## Architecture

```
                    bazaar.saffron-lang.org
                           |
              +------------+------------+
              |                         |
        Turmeric WASM            REST API Server
        (static site)            (Saffron @http/server)
                                        |
                    +-------------------+-------------------+
                    |                   |                   |
              Git Index          S3 Storage           PostgreSQL
         (public repo,          (tarballs)           (users, tokens,
       offline resolution)                            download stats)
```

- **Git-based index** (like crates.io) for fast offline resolution — clients `git pull` to sync
- **REST API server** written in Saffron using `@http/server`
- **Turmeric WASM frontend** for browsing, search, docs
- **Tarballs** stored on S3-compatible object storage
- **Index** is a public git repo with newline-delimited JSON entries per package

---

## Index Format

### Directory Structure

Uses the crates.io length-based scheme:

| Name length | Path |
|---|---|
| 1 char | `1/a` |
| 2 chars | `2/io` |
| 3 chars | `3/h/http` |
| 4+ chars | `tu/rm/turmeric` |

For 4+ character names: first two characters / next two characters / full name.

### Entry Format

Each file contains one JSON line per published version:

```json
{"name":"turmeric","vers":"0.1.0","deps":[{"name":"signal","req":"^1.0.0"}],"cksum":"sha256:a1b2c3d4e5f6...","yanked":false}
{"name":"turmeric","vers":"0.2.0","deps":[{"name":"signal","req":"^1.2.0"},{"name":"dom","req":"^0.3.0"}],"cksum":"sha256:f7e8d9c0...","yanked":false}
```

Full schema per line:

```json
{
  "name": "turmeric",
  "vers": "0.2.0",
  "deps": [
    {"name": "signal", "req": "^1.2.0", "optional": false},
    {"name": "dom", "req": "^0.3.0", "optional": true}
  ],
  "cksum": "sha256:f7e8d9c0b1a2...",
  "yanked": false,
  "features": {"default": ["dom"]},
  "saffron_version": ">=0.4.0"
}
```

---

## API Endpoints

### List / Search Packages

```
GET /api/v1/packages?page=1&per_page=20
GET /api/v1/search?q=http+server&page=1
```

Response:
```json
{
  "packages": [
    {"name": "turmeric", "description": "Reactive web framework", "latest": "0.2.0", "downloads": 1423},
    {"name": "httpx", "description": "HTTP client library", "latest": "1.0.3", "downloads": 892}
  ],
  "meta": {"total": 47, "page": 1}
}
```

### Package Info

```
GET /api/v1/packages/turmeric
```

Response:
```json
{
  "name": "turmeric",
  "description": "Reactive web framework for Saffron",
  "repository": "https://github.com/saffron-lang/turmeric",
  "license": "MIT",
  "owners": ["henry232323"],
  "versions": [
    {"vers": "0.2.0", "published": "2026-05-15T10:30:00Z", "downloads": 310, "yanked": false},
    {"vers": "0.1.0", "published": "2026-04-01T08:00:00Z", "downloads": 1113, "yanked": false}
  ]
}
```

### Download Tarball

```
GET /api/v1/packages/turmeric/0.2.0/download
```

Returns `302` redirect to S3 presigned URL.

### Publish

```
PUT /api/v1/packages/new
Authorization: Bearer <token>
Content-Type: application/octet-stream

<tarball bytes>
```

Response:
```json
{"ok": true, "package": "turmeric", "version": "0.2.0"}
```

### Yank

```
DELETE /api/v1/packages/turmeric/0.2.0/yank
Authorization: Bearer <token>
```

### Token Management

```
POST /api/v1/tokens
Authorization: Bearer <session-token>
Content-Type: application/json

{"name": "ci-publish", "scopes": ["publish"]}
```

Response:
```json
{"token": "bzr_a1b2c3d4e5f6g7h8...", "name": "ci-publish", "created": "2026-06-03T12:00:00Z"}
```

### Ownership

```
POST /api/v1/owners/turmeric
Authorization: Bearer <token>
Content-Type: application/json

{"add": ["contributor123"]}
```

```
DELETE /api/v1/owners/turmeric
Authorization: Bearer <token>
Content-Type: application/json

{"remove": ["old-maintainer"]}
```

---

## Package Format

### Contents of tarball (tar.gz)

```
turmeric-0.2.0/
  pantry.toml
  README.md
  LICENSE
  src/
    lib.sf
    signals.sf
    dom.sf
    router.sf
```

### Exclusions

Automatically excluded from publish:
- `.pantry/`, `build/`, `test/`, `.git/`
- `*.ll`, `*.o`, `*.so`, `*.dylib`
- `pantry.lock`, `pantry.sf`
- Files in `.pantryignore`

### Constraints

- Max tarball size: **50 MB**
- Max file count: **10,000 files**
- SHA-256 checksum recorded in index for verification
- No symlinks allowed in tarball
- `pantry.toml` must be present at root

---

## CLI Commands

```bash
# Authenticate via GitHub OAuth (opens browser)
pantry login

# Package and upload to Bazaar
pantry publish

# Search the registry
pantry search "web framework"
# => turmeric 0.2.0  - Reactive web framework for Saffron (1423 downloads)
# => httpx 1.0.3     - HTTP client library (892 downloads)

# Show package details
pantry info turmeric
# => turmeric 0.2.0
# => Reactive web framework for Saffron
# => License: MIT | Downloads: 1423 | Owners: henry232323
# => Versions: 0.2.0, 0.1.0

# Manage ownership
pantry owner add turmeric contributor123
pantry owner remove turmeric old-maintainer
```

---

## Resolution Algorithm

When `pantry install` encounters a registry dependency (`turmeric = "^0.2.0"`):

1. **Update local index cache** — `git pull` on `~/.pantry/registry/index/`
2. **Load package index entry** — read `tu/rm/turmeric` from local clone
3. **Find highest matching version** — parse all lines, filter yanked, find max satisfying semver constraint
4. **Check local cache** — look in `~/.pantry/cache/turmeric-0.2.0.tar.gz`
5. **Download if needed** — `GET /api/v1/packages/turmeric/0.2.0/download`
6. **Verify checksum** — compute SHA-256 of downloaded tarball, compare to index entry
7. **Extract** — untar to `.pantry/packages/turmeric-0.2.0/`
8. **Recurse** — resolve transitive dependencies (depth-first, version unification)
9. **Write lockfile** — record exact resolved versions in `pantry.lock`

Conflict resolution: if two packages require incompatible versions of the same dependency, emit an error with both constraint chains.

---

## Server Implementation (Saffron)

```saffron
import "@http/server" as Http
import "@crypto" as Crypto
import "@json" as Json

var server = Http.Server()

// Publish endpoint
server.put("/api/v1/packages/new", fun (req: Http.Request): Http.Response {
    var token = req.header("Authorization").replace("Bearer ", "")
    var user = authenticate(token)
    if (user == nil) {
        return Http.Response(401, Json.encode({"error": "unauthorized"}))
    }

    var tarball = req.body_bytes()
    if (tarball.length() > 50 * 1024 * 1024) {
        return Http.Response(413, Json.encode({"error": "tarball too large"}))
    }

    var cksum = Crypto.sha256_hex(tarball)
    var metadata = extract_pantry_toml(tarball)
    var name = metadata.get("name")
    var version = metadata.get("version")

    // Check ownership
    if (!is_owner(user, name)) {
        return Http.Response(403, Json.encode({"error": "not an owner"}))
    }

    // Check typosquatting
    if (is_typosquat(name)) {
        return Http.Response(400, Json.encode({"error": "name too similar to existing package"}))
    }

    // Upload tarball to S3
    upload_tarball(name, version, tarball)

    // Update git index
    var entry = Json.encode({
        "name": name, "vers": version,
        "deps": metadata.get("deps"),
        "cksum": "sha256:${cksum}", "yanked": false
    })
    commit_to_index(name, entry)

    return Http.Response(200, Json.encode({"ok": true, "package": name, "version": version}))
})

// Download endpoint
server.get("/api/v1/packages/:name/:version/download", fun (req: Http.Request): Http.Response {
    var name = req.param("name")
    var version = req.param("version")
    var url = presign_s3_url(name, version)
    return Http.Response(302, "", {"Location": url})
})

// Search endpoint
server.get("/api/v1/search", fun (req: Http.Request): Http.Response {
    var query = req.query("q")
    var results = search_packages(query)
    return Http.Response(200, Json.encode({"packages": results}))
})

server.listen(8080)
IO.println("Bazaar running on :8080")
```

---

## Frontend (Turmeric)

The web frontend is a Turmeric WASM application served as static files.

### Pages

- **Home** — search bar, trending packages (most downloaded this week), recently published
- **Package page** — README (rendered markdown), install command (`pantry add turmeric`), version history, dependency tree visualization
- **Docs** — auto-generated API documentation from `///` docstrings in source
- **User pages** — list of owned/published packages

### Example Component

```saffron
import "@turmeric" as T
import "@turmeric/router" as Router

fun PackagePage(props: {name: String}): T.Element {
    var pkg = T.signal(nil)
    var loading = T.signal(true)

    T.effect(fun () {
        fetch_package(props.name, fun (data) {
            pkg.set(data)
            loading.set(false)
        })
    })

    return T.html("
        <div class='package-page'>
            <h1>${props.name}</h1>
            <code>pantry add ${props.name}</code>
            <div class='readme'>${pkg.get().readme}</div>
            <aside class='sidebar'>
                <h3>Versions</h3>
                ${render_versions(pkg.get().versions)}
            </aside>
        </div>
    ")
}
```

---

## Security

### Authentication

- **CLI login:** PKCE OAuth flow with GitHub — opens browser, receives code via localhost callback
- **API tokens:** bearer tokens (`bzr_<random>`) hashed with SHA-256 server-side
- **Scopes:** `publish`, `yank`, `owner` — tokens can be scoped to specific permissions

### Integrity

- SHA-256 checksums stored in the git index; clients verify after download
- Index commits are GPG-signed by the server
- No symlinks allowed in tarballs (prevents path traversal)

### Abuse Prevention

- **Typosquatting detection:** Levenshtein distance check against top 100 packages on publish
- **Rate limiting:** 10 publishes/hour per user, 100 downloads/minute per IP
- **Name squatting:** packages unused for 6 months with 0 downloads can be reclaimed
- **Malware scanning:** future phase — run published packages in sandbox, flag suspicious syscalls

---

## Implementation Phases

### Phase 1: Local Registry Prototype

- Tarball creation in `pantry publish` (shell out to `tar`)
- Git index creation and commit logic
- `resolve_registry_dep()` reads local index, downloads from file:// URL
- Checksum verification

### Phase 2: API Server

- Saffron HTTP server with path-parameter routing
- S3 upload/download with presigned URLs
- Token auth, user table in PostgreSQL
- Index commit automation (server pushes to git)

### Phase 3: CLI Polish

- `pantry search` with formatted output
- `pantry info` with full package details
- `pantry owner add/remove`
- Progress bars for upload/download
- `pantry login` with PKCE OAuth

### Phase 4: Turmeric Frontend

- WASM website at `bazaar.saffron-lang.org`
- Package browsing, search, README rendering
- User accounts, token management UI
- Dependency graph visualization

### Phase 5: Ecosystem Growth

- Auto-generated docs from `///` docstrings
- Download statistics and trending
- Categories and keywords
- Badge generation (`![version](bazaar.saffron-lang.org/badge/turmeric)`)
- Webhooks for CI integration

---

## Prerequisites Needed

| Prerequisite | Status | Notes |
|---|---|---|
| `@crypto` module (SHA-256) | Not started | Needed for checksums and token hashing |
| tar/gzip creation | Workaround | Shell out to system `tar` initially |
| Path-parameter routing in `@http/server` | Not started | `:name` and `:version` extraction |
| Working async/networking in native builds | Done | Async fully functional at O2 |
| PostgreSQL client | Not started | For user/token/stats storage |
| S3 client | Not started | For tarball storage |
