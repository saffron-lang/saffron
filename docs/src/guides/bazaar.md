# Bazaar Package Registry

Bazaar is the official package registry for Saffron. It hosts community libraries and integrates with the `pantry` build tool for dependency management.

## Overview

Bazaar provides:

- A central registry for Saffron packages
- Versioned releases with SemVer
- Full-text package search
- CLI integration via `pantry`
- A web frontend for browsing packages

## Searching for packages

### CLI

```bash
pantry search json
```

Output:

```
json_parser  v1.2.0  — Fast JSON parser for Saffron
json_schema  v0.3.1  — JSON Schema validation
```

### Web

Visit [bazaar.saffron-lang.org](https://bazaar.saffron-lang.org) to browse and search packages in your browser.

## Adding a dependency

```bash
pantry add json_parser
```

This updates your `pantry.toml`:

```toml
[dependencies]
json_parser = "1.2.0"
```

Then use it in your code:

```saffron
import "json_parser" as JSON

var data = JSON.parse("{\"name\": \"saffron\"}")
IO.println(data.get("name"))
```

### Specifying versions

```bash
pantry add json_parser@1.2.0      # exact version
pantry add json_parser@^1.0       # compatible with 1.x
```

In `pantry.toml`:

```toml
[dependencies]
json_parser = "^1.0"     # any 1.x release
http_client = "=2.3.1"   # exactly 2.3.1
```

## Publishing a package

### 1. Log in

```bash
pantry login
```

This opens the Bazaar web UI for authentication and stores a token locally.

### 2. Prepare your `pantry.toml`

```toml
[package]
name = "my-library"
version = "0.1.0"
description = "A useful library for Saffron"
authors = ["you"]
license = "MIT"
repository = "https://github.com/you/my-library"

[dependencies]
# list your deps here
```

Required fields for publishing: `name`, `version`, `description`, `authors`.

### 3. Publish

```bash
pantry publish
```

This packages your source, uploads it to Bazaar, and makes it available for others to `pantry add`.

### Versioning rules

- Follow [Semantic Versioning](https://semver.org/): `MAJOR.MINOR.PATCH`
- Breaking changes require a major version bump
- You cannot re-publish the same version -- bump the version number for each release

## Yanking a version

If you publish a broken release, you can yank it (prevents new installs, but existing lockfiles still resolve):

```bash
pantry yank my-library@0.1.0
```

## API reference

The Bazaar server exposes a JSON API:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/packages` | GET | List all packages |
| `/api/v1/packages/:name` | GET | Get package details and versions |
| `/api/v1/packages/:name/:version/download` | GET | Download a package tarball |
| `/api/v1/packages/new` | PUT | Publish a new version (auth required) |
| `/api/v1/packages/:name/:version/yank` | DELETE | Yank a version (auth required) |
| `/api/v1/search?q=query` | GET | Full-text search |

## Self-hosting

Bazaar is itself a Saffron application (built with the `@http/server` stdlib). To run your own registry:

```bash
git clone https://github.com/saffron-lang/saffron.git
cd saffron/bazaar
pantry build
./build/bazaar
```

Configure via environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `BAZAAR_PORT` | `3000` | HTTP port |

## Project structure

```
bazaar/
  pantry.toml           # workspace manifest
  src/
    main.sf             # server entry point
    routes.sf           # API route handlers
    db.sf               # SQLite database layer
    auth.sf             # token-based authentication
    search.sf           # full-text search index
    storage.sf          # package file storage
    index.sf            # git-backed package index
  frontend/
    pantry.toml         # frontend sub-package
    src/main.sf         # Turmeric SPA (browse/search UI)
    public/
      index.html
      style.css
  migrations/
    001_initial.sql     # database schema
```
