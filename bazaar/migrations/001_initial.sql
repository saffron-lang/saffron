CREATE TABLE IF NOT EXISTS packages (
    name TEXT PRIMARY KEY,
    description TEXT DEFAULT '',
    repository TEXT DEFAULT '',
    license TEXT DEFAULT '',
    created_at TEXT DEFAULT (datetime('now')),
    total_downloads INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS versions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_name TEXT NOT NULL REFERENCES packages(name),
    version TEXT NOT NULL,
    checksum TEXT NOT NULL,
    published_by TEXT DEFAULT '',
    published_at TEXT DEFAULT (datetime('now')),
    yanked INTEGER DEFAULT 0,
    UNIQUE(package_name, version)
);

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    created_at TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS tokens (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    token_hash TEXT UNIQUE NOT NULL,
    user_id INTEGER REFERENCES users(id),
    created_at TEXT DEFAULT (datetime('now')),
    revoked INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS downloads (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_name TEXT NOT NULL,
    version TEXT NOT NULL,
    downloaded_at TEXT DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS owners (
    package_name TEXT NOT NULL REFERENCES packages(name),
    user_id INTEGER NOT NULL REFERENCES users(id),
    PRIMARY KEY (package_name, user_id)
);
