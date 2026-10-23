#!/usr/bin/env python3
"""opg registry: a small package registry for Oker packages.

Storage: SQLite for metadata, one .tar.gz file per published version.

Run:
    python server.py create-user alice      # prints a publish token (shown once)
    python server.py serve                  # http://127.0.0.1:4873

API (all JSON except the download):
    GET  /api/packages/<name>                    versions + latest
    GET  /api/packages/<name>/<version>          metadata (sha256, dependencies)
    GET  /api/packages/<name>/<version>/download the .tar.gz
    PUT  /api/packages/<name>/<version>          publish (Bearer token, body = .tar.gz)

This is a development server (Flask's built-in). For anything public, run it
behind a real WSGI server and TLS.
"""

import argparse
import hashlib
import io
import json
import os
import re
import secrets
import sqlite3
import sys
import tarfile
from datetime import datetime, timezone

from flask import Flask, g, jsonify, request, send_file

DATA_DIR = os.path.abspath(os.environ.get("OPG_DATA_DIR", "./data"))
DB_PATH = os.path.join(DATA_DIR, "registry.db")
PKG_DIR = os.path.join(DATA_DIR, "packages")

MAX_UPLOAD_BYTES = 50 * 1024 * 1024
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_MEMBERS = 20000
MAX_UNPACKED_BYTES = 512 * 1024 * 1024

NAME_RE = re.compile(r"^[a-z][a-z0-9_-]{0,63}$")
VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$"
)

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = MAX_UPLOAD_BYTES


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

def now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def error(status, message):
    resp = jsonify({"error": message})
    resp.status_code = status
    return resp


def version_key(version):
    """Sort key: higher is newer. Pre-releases sort below their release."""
    m = VERSION_RE.match(version)
    major, minor, patch = int(m.group(1)), int(m.group(2)), int(m.group(3))
    pre = m.group(4)
    if pre is None:
        return (major, minor, patch, 1, ())
    parts = []
    for ident in pre[1:].split("."):
        # numeric identifiers sort below alphanumeric ones, numerically
        parts.append((0, int(ident), "") if ident.isdigit() else (1, 0, ident))
    return (major, minor, patch, 0, tuple(parts))


def get_db():
    if "db" not in g:
        g.db = sqlite3.connect(DB_PATH, timeout=10)
        g.db.row_factory = sqlite3.Row
        g.db.execute("PRAGMA foreign_keys = ON")
    return g.db


@app.teardown_appcontext
def close_db(_exc):
    db = g.pop("db", None)
    if db is not None:
        db.close()


def init_storage():
    os.makedirs(PKG_DIR, exist_ok=True)
    db = sqlite3.connect(DB_PATH)
    db.executescript(
        """
        CREATE TABLE IF NOT EXISTS users (
            id          INTEGER PRIMARY KEY,
            username    TEXT NOT NULL UNIQUE,
            token_hash  TEXT NOT NULL UNIQUE,
            created_at  TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS packages (
            name      TEXT PRIMARY KEY,
            owner_id  INTEGER NOT NULL REFERENCES users(id)
        );
        CREATE TABLE IF NOT EXISTS versions (
            name          TEXT NOT NULL REFERENCES packages(name),
            version       TEXT NOT NULL,
            sha256        TEXT NOT NULL,
            size          INTEGER NOT NULL,
            manifest      TEXT NOT NULL,
            published_at  TEXT NOT NULL,
            PRIMARY KEY (name, version)
        );
        """
    )
    db.commit()
    db.close()


def current_user():
    header = request.headers.get("Authorization", "")
    if not header.startswith("Bearer "):
        return None
    token = header[len("Bearer "):].strip()
    if not token:
        return None
    token_hash = hashlib.sha256(token.encode()).hexdigest()
    return get_db().execute(
        "SELECT id, username FROM users WHERE token_hash = ?", (token_hash,)
    ).fetchone()


def valid_member_path(path):
    if not path or "\\" in path or "\x00" in path or path.startswith("/"):
        return False
    parts = path.rstrip("/").split("/")
    return all(p not in ("", ".", "..") for p in parts)


def validate_dependencies(deps, own_name):
    if not isinstance(deps, dict):
        raise ValueError("'dependencies' must be an object")
    for dep, req in deps.items():
        if not NAME_RE.match(dep):
            raise ValueError("invalid dependency name '%s'" % dep)
        if dep == own_name:
            raise ValueError("a package cannot depend on itself")
        if not isinstance(req, str) or not VERSION_RE.match(req):
            raise ValueError(
                "dependency '%s' must be pinned to an exact version like 1.2.3" % dep
            )


def validate_package(data, name, version):
    """Check an uploaded archive; return its parsed oker.json.

    The same rules protect the client's extractor, but the registry must not
    trust that, so it refuses anything unsafe or malformed up front.
    """
    try:
        tf = tarfile.open(fileobj=io.BytesIO(data), mode="r:gz")
    except (tarfile.TarError, OSError, EOFError):
        raise ValueError("upload is not a valid .tar.gz archive")

    manifest_bytes = None
    count = 0
    unpacked = 0
    try:
        with tf:
            for member in tf:
                count += 1
                if count > MAX_MEMBERS:
                    raise ValueError("archive has too many entries")
                if member.pax_headers:
                    raise ValueError(
                        "archive uses tar extensions opg cannot install; "
                        "create it with 'opg pack'"
                    )
                if not (member.isreg() or member.isdir()):
                    raise ValueError(
                        "'%s' is not a regular file or directory" % member.name
                    )
                if not valid_member_path(member.name):
                    raise ValueError("unsafe path in archive: '%s'" % member.name)
                top = member.name.split("/", 1)[0]
                if top != "package":
                    raise ValueError(
                        "every entry must live under 'package/' (found '%s')" % member.name
                    )
                unpacked += member.size
                if unpacked > MAX_UNPACKED_BYTES:
                    raise ValueError("archive is too large when unpacked")
                if member.name == "package/oker.json":
                    if not member.isreg() or member.size > MAX_MANIFEST_BYTES:
                        raise ValueError("oker.json is invalid or too large")
                    manifest_bytes = tf.extractfile(member).read()
    except (tarfile.TarError, OSError, EOFError):
        raise ValueError("archive is corrupted")

    if manifest_bytes is None:
        raise ValueError("archive must contain package/oker.json")
    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise ValueError("package/oker.json is not valid JSON")
    if not isinstance(manifest, dict):
        raise ValueError("package/oker.json must be a JSON object")
    if manifest.get("name") != name:
        raise ValueError(
            "name in oker.json ('%s') does not match the URL ('%s')"
            % (manifest.get("name"), name)
        )
    if manifest.get("version") != version:
        raise ValueError(
            "version in oker.json ('%s') does not match the URL ('%s')"
            % (manifest.get("version"), version)
        )
    validate_dependencies(manifest.get("dependencies") or {}, name)
    return manifest


def archive_path(name, version):
    # name/version were validated against strict regexes, so this cannot escape.
    return os.path.join(PKG_DIR, name, version + ".tar.gz")


# --------------------------------------------------------------------------
# routes
# --------------------------------------------------------------------------

@app.get("/")
def index():
    count = get_db().execute("SELECT COUNT(*) FROM packages").fetchone()[0]
    return jsonify({"registry": "opg", "packages": count})


@app.get("/api/packages/<name>")
def package_info(name):
    if not NAME_RE.match(name):
        return error(400, "invalid package name")
    db = get_db()
    pkg = db.execute(
        "SELECT p.name, u.username AS owner FROM packages p "
        "JOIN users u ON u.id = p.owner_id WHERE p.name = ?",
        (name,),
    ).fetchone()
    if pkg is None:
        return error(404, "package '%s' not found" % name)
    rows = db.execute(
        "SELECT version, sha256, size, published_at FROM versions WHERE name = ?",
        (name,),
    ).fetchall()
    rows = sorted(rows, key=lambda r: version_key(r["version"]))
    versions = [dict(r) for r in rows]
    releases = [v["version"] for v in versions if "-" not in v["version"]]
    latest = (releases or [v["version"] for v in versions] or [None])[-1]
    return jsonify(
        {"name": name, "owner": pkg["owner"], "latest": latest, "versions": versions}
    )


@app.get("/api/packages/<name>/<version>")
def version_info(name, version):
    if not NAME_RE.match(name) or not VERSION_RE.match(version):
        return error(400, "invalid package name or version")
    row = get_db().execute(
        "SELECT sha256, size, manifest, published_at FROM versions "
        "WHERE name = ? AND version = ?",
        (name, version),
    ).fetchone()
    if row is None:
        return error(404, "%s@%s not found" % (name, version))
    manifest = json.loads(row["manifest"])
    return jsonify(
        {
            "name": name,
            "version": version,
            "sha256": row["sha256"],
            "size": row["size"],
            "description": manifest.get("description", ""),
            "dependencies": manifest.get("dependencies") or {},
            "published_at": row["published_at"],
        }
    )


@app.get("/api/packages/<name>/<version>/download")
def download(name, version):
    if not NAME_RE.match(name) or not VERSION_RE.match(version):
        return error(400, "invalid package name or version")
    row = get_db().execute(
        "SELECT sha256 FROM versions WHERE name = ? AND version = ?", (name, version)
    ).fetchone()
    path = archive_path(name, version)
    if row is None or not os.path.isfile(path):
        return error(404, "%s@%s not found" % (name, version))
    resp = send_file(
        path,
        mimetype="application/gzip",
        as_attachment=True,
        download_name="%s-%s.tar.gz" % (name, version),
    )
    resp.headers["X-Opg-Sha256"] = row["sha256"]
    return resp


@app.put("/api/packages/<name>/<version>")
def publish(name, version):
    if not NAME_RE.match(name):
        return error(400, "invalid package name (lowercase letters, digits, '-' or '_')")
    if not VERSION_RE.match(version):
        return error(400, "invalid version (expected MAJOR.MINOR.PATCH)")

    user = current_user()
    if user is None:
        return error(401, "missing or invalid token")

    data = request.get_data()
    if not data:
        return error(400, "empty upload")

    digest = hashlib.sha256(data).hexdigest()
    claimed = request.headers.get("X-Opg-Sha256")
    if claimed and claimed.lower() != digest:
        return error(400, "checksum mismatch: the upload was corrupted in transit")

    try:
        manifest = validate_package(data, name, version)
    except ValueError as exc:
        return error(400, str(exc))

    db = get_db()
    db.isolation_level = None  # manage the transaction explicitly
    db.execute("BEGIN IMMEDIATE")
    try:
        pkg = db.execute(
            "SELECT owner_id FROM packages WHERE name = ?", (name,)
        ).fetchone()
        if pkg is not None and pkg["owner_id"] != user["id"]:
            db.execute("ROLLBACK")
            return error(403, "package '%s' is owned by another user" % name)
        exists = db.execute(
            "SELECT 1 FROM versions WHERE name = ? AND version = ?", (name, version)
        ).fetchone()
        if exists:
            db.execute("ROLLBACK")
            return error(
                409,
                "%s@%s is already published; versions are immutable, bump the version"
                % (name, version),
            )

        path = archive_path(name, version)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + ".part-%s" % secrets.token_hex(4)
        with open(tmp, "wb") as fh:
            fh.write(data)
        os.replace(tmp, path)

        if pkg is None:
            db.execute(
                "INSERT INTO packages (name, owner_id) VALUES (?, ?)", (name, user["id"])
            )
        db.execute(
            "INSERT INTO versions (name, version, sha256, size, manifest, published_at) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            (name, version, digest, len(data), json.dumps(manifest), now_iso()),
        )
        db.execute("COMMIT")
    except Exception:
        try:
            db.execute("ROLLBACK")
        except sqlite3.Error:
            pass
        raise

    resp = jsonify({"name": name, "version": version, "sha256": digest, "size": len(data)})
    resp.status_code = 201
    return resp


@app.errorhandler(404)
def not_found(_e):
    return error(404, "not found")


@app.errorhandler(405)
def method_not_allowed(_e):
    return error(405, "method not allowed")


@app.errorhandler(413)
def too_large(_e):
    return error(413, "upload too large (limit %d MB)" % (MAX_UPLOAD_BYTES // (1024 * 1024)))


@app.errorhandler(500)
def server_error(_e):
    return error(500, "internal server error")


# --------------------------------------------------------------------------
# command line
# --------------------------------------------------------------------------

def cmd_create_user(username):
    if not re.match(r"^[A-Za-z0-9_.-]{1,64}$", username):
        sys.exit("error: username may contain letters, digits, '.', '_' and '-'")
    init_storage()
    token = secrets.token_urlsafe(32)
    db = sqlite3.connect(DB_PATH)
    try:
        db.execute(
            "INSERT INTO users (username, token_hash, created_at) VALUES (?, ?, ?)",
            (username, hashlib.sha256(token.encode()).hexdigest(), now_iso()),
        )
        db.commit()
    except sqlite3.IntegrityError:
        sys.exit("error: user '%s' already exists" % username)
    finally:
        db.close()
    print("Created user '%s'." % username)
    print("Publish token (shown once, store it safely):\n")
    print("  " + token + "\n")
    print("Use it with:  export OPG_TOKEN=" + token)


def main():
    parser = argparse.ArgumentParser(description="opg registry server")
    sub = parser.add_subparsers(dest="command", required=True)

    cu = sub.add_parser("create-user", help="create a user and print a publish token")
    cu.add_argument("username")

    sv = sub.add_parser("serve", help="run the development server")
    sv.add_argument("--host", default="127.0.0.1")
    sv.add_argument("--port", type=int, default=4873)

    args = parser.parse_args()
    if args.command == "create-user":
        cmd_create_user(args.username)
    else:
        init_storage()
        print("opg registry: data in %s" % DATA_DIR)
        app.run(host=args.host, port=args.port)


if __name__ == "__main__":
    main()
