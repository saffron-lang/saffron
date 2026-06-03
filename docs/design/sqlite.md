# @sqlite — Optional SQLite Module

Optional module providing embedded database access. Available when `sqlite3` is installed on the system. Two backends: **FFI** (fast, links `libsqlite3` directly) and **CLI fallback** (shells out to `sqlite3` binary for environments where linking isn't possible).

---

## API

### Database

```saffron
import "@sqlite" as Sqlite

var db = Sqlite.open("app.db")       // opens or creates file
var db = Sqlite.open(":memory:")     // in-memory database

db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT)")
db.exec("INSERT INTO users (name, email) VALUES ('alice', 'alice@example.com')")

var rows = db.query_all("SELECT * FROM users WHERE name LIKE ?", ["%ali%"])
var row = db.query_one("SELECT count(*) as n FROM users")
var count = row.get_int("n")

db.close()
```

### Row

```saffron
class Row {
    fun get(column: String): String       // get column value as string
    fun get_int(column: String): Int      // get as integer
    fun get_float(column: String): Float  // get as float
    fun columns(): List<String>           // column names in this row
    fun to_map(): Map<String, String>     // all columns as key-value map
}
```

### Statement (Prepared)

```saffron
var stmt = db.prepare("INSERT INTO users (name, email) VALUES (?, ?)")
stmt.run(["bob", "bob@example.com"])
stmt.run(["carol", "carol@example.com"])
stmt.finalize()

var select_stmt = db.prepare("SELECT * FROM users WHERE id > ?")
var rows = select_stmt.query([5])
select_stmt.finalize()
```

### Transactions (Trailing Closure)

```saffron
db.transaction {
    exec("INSERT INTO orders (user_id, total) VALUES (1, 99.50)")
    exec("UPDATE users SET order_count = order_count + 1 WHERE id = 1")
}
// auto-commits on success, auto-rolls-back on throw
```

### Query with Row Callback

```saffron
db.query("SELECT * FROM users ORDER BY name") { row =>
    IO.println("${row.get("name")}: ${row.get("email")}")
}
```

### Migrations

```saffron
Sqlite.migrate(db, "migrations/")   // apply pending .sql files
Sqlite.rollback(db, "migrations/")  // undo last migration
```

---

## Example: Full CRUD

```saffron
import "@sqlite" as Sqlite

var db = Sqlite.open("blog.db")

db.exec("
    CREATE TABLE IF NOT EXISTS posts (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        title TEXT NOT NULL,
        body TEXT NOT NULL,
        created_at TEXT DEFAULT (datetime('now'))
    )
")

// Insert with trailing closure transaction
db.transaction {
    exec("INSERT INTO posts (title, body) VALUES (?, ?)", ["Hello", "First post!"])
    exec("INSERT INTO posts (title, body) VALUES (?, ?)", ["Second", "Another one"])
}

// Query with row callback
db.query("SELECT * FROM posts ORDER BY created_at DESC") { row =>
    IO.println("[${row.get("id")}] ${row.get("title")} — ${row.get("created_at")}")
}

// Single result
var count = db.query_one("SELECT count(*) as n FROM posts").get_int("n")
IO.println("Total posts: ${count}")

db.close()
```

---

## FFI Layer

When `libsqlite3` is detected, the module links directly via `@extern`:

```saffron
// Internal declarations in @sqlite implementation
@extern("i32 sqlite3_open(i8*, i8**)")
fun _sqlite3_open(filename: Int, db_out: Int): Int

@extern("i32 sqlite3_prepare_v2(i8*, i8*, i32, i8**, i8**)")
fun _sqlite3_prepare_v2(db: Int, sql: Int, nbytes: Int, stmt_out: Int, tail: Int): Int

@extern("i32 sqlite3_step(i8*)")
fun _sqlite3_step(stmt: Int): Int

@extern("i32 sqlite3_bind_text(i8*, i32, i8*, i32, i8*)")
fun _sqlite3_bind_text(stmt: Int, idx: Int, text: Int, len: Int, destructor: Int): Int

@extern("i32 sqlite3_bind_int64(i8*, i32, i64)")
fun _sqlite3_bind_int64(stmt: Int, idx: Int, value: Int): Int

@extern("i8* sqlite3_column_text(i8*, i32)")
fun _sqlite3_column_text(stmt: Int, col: Int): Int

@extern("i32 sqlite3_column_int(i8*, i32)")
fun _sqlite3_column_int(stmt: Int, col: Int): Int

@extern("double sqlite3_column_double(i8*, i32)")
fun _sqlite3_column_double(stmt: Int, col: Int): Float

@extern("i32 sqlite3_column_count(i8*)")
fun _sqlite3_column_count(stmt: Int): Int

@extern("i8* sqlite3_column_name(i8*, i32)")
fun _sqlite3_column_name(stmt: Int, col: Int): Int

@extern("i32 sqlite3_finalize(i8*)")
fun _sqlite3_finalize(stmt: Int): Int

@extern("i32 sqlite3_close(i8*)")
fun _sqlite3_close(db: Int): Int

@extern("i8* sqlite3_errmsg(i8*)")
fun _sqlite3_errmsg(db: Int): Int
```

Linking: `tools/saffron build app.sf -o app -l sqlite3`

---

## CLI Fallback

When `libsqlite3` is not available for linking (or `--no-ffi-sqlite` is passed), the module falls back to shelling out:

```bash
echo "SELECT * FROM users;" | sqlite3 -header -separator '\t' app.db
```

The Saffron wrapper:
1. Spawns `sqlite3 -header -separator '\t' <dbfile>`
2. Pipes SQL via stdin
3. Parses TSV output: first line = column headers, remaining lines = data
4. Constructs `Row` objects from parsed columns

Limitations of CLI fallback:
- No prepared statements (each exec spawns a process)
- No concurrent access (file locking depends on sqlite3 CLI)
- Slower (~5-50x) than FFI for bulk operations
- No in-memory databases (`:memory:` not supported)

Detection at build time:
```bash
# tools/saffron checks for libsqlite3
if pkg-config --exists sqlite3 2>/dev/null; then
    SQLITE_MODE="ffi"
    LINK_FLAGS="$LINK_FLAGS $(pkg-config --libs sqlite3)"
else
    SQLITE_MODE="cli"
fi
```

---

## Optional Dependency Mechanism

In `pantry.toml`:

```toml
[system-dependencies]
sqlite3 = { required = false, pkg-config = "sqlite3", min-version = "3.35.0" }
```

Semantics:
- `required = false` — build succeeds without it; module degrades to CLI fallback
- `pkg-config` — name passed to `pkg-config --exists` and `pkg-config --libs`
- `min-version` — checked via `pkg-config --modversion`
- At build time, `tools/saffron` adds `-lsqlite3` and relevant `-L` paths when detected
- `--link sqlite3` flag forces FFI mode (fails if library not found)
- `--no-link sqlite3` forces CLI fallback

---

## Error Handling

All errors throw with a consistent format:

```saffron
// Pattern: "sqlite: <operation>: <detail>"
// Examples:
// "sqlite: open: unable to open database file"
// "sqlite: exec: UNIQUE constraint failed: users.email"
// "sqlite: prepare: near \"SELEC\": syntax error"
// "sqlite: step: database is locked"

try {
    db.exec("INSERT INTO users (email) VALUES ('duplicate@x.com')")
} catch (e) {
    IO.println(e)  // "sqlite: exec: UNIQUE constraint failed: users.email"
}
```

The FFI backend reads `sqlite3_errmsg()` for detail. The CLI backend parses stderr from the sqlite3 process.

---

## Migration System

Numbered SQL files in a `migrations/` directory:

```
migrations/
├── 001_create_users.sql
├── 002_add_email_index.sql
└── 003_create_orders.sql
```

Tracking table (auto-created):

```sql
CREATE TABLE IF NOT EXISTS _sf_migrations (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TEXT DEFAULT (datetime('now'))
);
```

Behavior:
- `Sqlite.migrate(db, "migrations/")` — applies all `.sql` files not yet in `_sf_migrations`, in numeric order
- `Sqlite.rollback(db, "migrations/")` — undoes the last applied migration (looks for corresponding `001_create_users_down.sql` or `-- @down` section in the file)
- Each migration runs inside a transaction
- If a migration fails, it rolls back that single migration and throws

Down migration format (either separate file or inline):

```sql
-- 001_create_users.sql
CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT UNIQUE);

-- @down
DROP TABLE users;
```

---

## Bazaar Usage Example

The Bazaar registry server uses `@sqlite` for package metadata, search, and download stats:

```saffron
import "@sqlite" as Sqlite
import "@http/server" as Http

var db = Sqlite.open("/var/bazaar/registry.db")

// Apply schema migrations on startup
Sqlite.migrate(db, "migrations/")

// Search endpoint
server.get("/api/v1/search", fun (req: Http.Request): Http.Response {
    var q = req.query("q")
    var rows = db.query_all(
        "SELECT name, description, latest_version, downloads FROM packages WHERE name LIKE ? ORDER BY downloads DESC LIMIT 20",
        ["%${q}%"]
    )
    return Http.Response(200, Json.encode({"packages": rows}))
})

// Download counter (transaction for atomicity)
server.get("/api/v1/packages/:name/:version/download", fun (req: Http.Request): Http.Response {
    var name = req.param("name")
    var version = req.param("version")
    db.transaction {
        exec("UPDATE packages SET downloads = downloads + 1 WHERE name = ?", [name])
        exec("INSERT INTO download_log (package, version, at) VALUES (?, ?, datetime('now'))", [name, version])
    }
    return Http.Response(302, "", {"Location": presign_url(name, version)})
})

// Token authentication
fun authenticate(token: String): String {
    var row = db.query_one(
        "SELECT username FROM tokens WHERE token_hash = ? AND revoked = 0",
        [Crypto.sha256_hex(token)]
    )
    return row.get("username")
}
```

---

## Implementation Phases

### Phase 1: CLI Backend (no compiler changes)

- Implement `Sqlite.open/close/exec/query_all/query_one` using process spawn
- Parse TSV output into Row objects
- Transaction support via `BEGIN`/`COMMIT`/`ROLLBACK` SQL
- Migration system (file scanning, tracking table)
- Ship as `src/lib/sqlite.sf`

### Phase 2: FFI Backend

- Add `@extern` declarations for sqlite3 C API
- Implement prepared statement lifecycle (prepare/bind/step/finalize)
- Memory management: ensure stmt handles are finalized on GC or scope exit
- Benchmark against CLI backend

### Phase 3: Optional Dependency Detection

- `pkg-config` detection in `tools/saffron`
- `[system-dependencies]` section in pantry.toml schema
- Conditional compilation: FFI path vs CLI path selected at build time
- `--link` / `--no-link` override flags

### Phase 4: Bazaar Integration

- Migrate Bazaar from PostgreSQL to SQLite for single-node deployments
- Full-text search via FTS5 extension
- WAL mode for concurrent readers
- Backup/export tooling
