-- FTS5 virtual table for package search
CREATE VIRTUAL TABLE IF NOT EXISTS packages_fts USING fts5(
    name,
    description,
    content='packages',
    content_rowid='rowid'
);

-- Populate FTS from existing packages
INSERT INTO packages_fts(packages_fts) VALUES('rebuild');

-- Triggers to keep FTS in sync
CREATE TRIGGER IF NOT EXISTS packages_ai AFTER INSERT ON packages BEGIN
    INSERT INTO packages_fts(rowid, name, description) VALUES (new.rowid, new.name, new.description);
END;

CREATE TRIGGER IF NOT EXISTS packages_ad AFTER DELETE ON packages BEGIN
    INSERT INTO packages_fts(packages_fts, rowid, name, description) VALUES ('delete', old.rowid, old.name, old.description);
END;

CREATE TRIGGER IF NOT EXISTS packages_au AFTER UPDATE ON packages BEGIN
    INSERT INTO packages_fts(packages_fts, rowid, name, description) VALUES ('delete', old.rowid, old.name, old.description);
    INSERT INTO packages_fts(rowid, name, description) VALUES (new.rowid, new.name, new.description);
END;
