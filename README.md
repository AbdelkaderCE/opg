# opg: the Oker package manager

`opg` is a small C++ command-line client plus a Python registry server.

| Command | What it does |
|---|---|
| `opg init` | Create `oker.json` |
| `opg pack` | Bundle the project into a reproducible `.tar.gz` and print its SHA-256 |
| `opg publish` | Upload the package to a registry |
| `opg install [name[@version]...]` | Install dependencies into `oker_modules/` and write `oker.lock` |
| `opg install --frozen` | CI mode: fail unless `oker.lock` matches `oker.json`; change nothing |
| `opg resolve <import> [--from file]` | Show which file an import would load (debugging aid) |

## Build the client

Needs a C++17 compiler, CMake 3.20+, git, and the zlib and OpenSSL development packages.

```bash
sudo apt install zlib1g-dev libssl-dev      # Debian/Ubuntu
cmake -S . -B build
cmake --build build
# the program is build/opg
```

## Run a registry

```bash
cd registry
pip install -r requirements.txt
python server.py create-user alice          # prints a publish token, shown once
python server.py serve                      # http://127.0.0.1:4873
```

Data (SQLite database and package archives) is stored in `registry/data/`.
Change the location with `OPG_DATA_DIR`. The built-in Flask server is for
development; put a real WSGI server and HTTPS in front of it for anything public.

## Typical workflow

```bash
export OPG_TOKEN=<token from create-user>
export OPG_REGISTRY=http://127.0.0.1:4873   # this is also the default

# author a library
mkdir mylib && cd mylib
opg init
mkdir src && echo '...' > src/main.oker
opg publish

# use it from another project
cd ../myapp
opg init
opg install mylib                           # adds "mylib": "<latest>" to oker.json
opg install                                 # installs everything listed in oker.json
```

## The lockfile (`oker.lock`)

`opg install` records the exact version and SHA-256 of every package it installed,
including dependencies of dependencies. Commit this file.

- `opg install` with a matching `oker.lock` installs exactly what it lists and does not
  re-resolve anything. Each download is verified against the **locked** hash, so a package
  that changed on the registry after you locked it is rejected, not silently accepted.
- If you edit `oker.json` by hand, the lock no longer matches. A plain `opg install`
  re-resolves and rewrites it; `opg install --frozen` refuses (use it in CI).
- If the registry reports a different hash for a version that is already locked, `opg`
  stops and leaves `oker.json` and `oker.lock` untouched.
- The file is sorted and deterministic: the same dependencies always give the same bytes.

## Wiring imports into the Oker compiler

`src/resolver.hpp` and `src/resolver.cpp` are self-contained (standard library plus
nlohmann/json). Add those two files to the compiler's build and call:

```cpp
#include "resolver.hpp"

opg::ResolveResult r = opg::resolveImport(importString, currentSourceFile);
if (!r.found) {
    reportError(r.error);          // already a readable message
} else {
    loadModule(r.path);            // absolute, normalised path
}
```

Rules:

| Import string | Resolved to |
|---|---|
| `./util`, `../lib/x` | relative to the importing file; tries `<path>` then `<path>.oker` |
| `libmath` | `oker_modules/libmath/` + its `main` from `oker.json` (default `src/main.oker`) |
| `libmath/sub/mod` | `oker_modules/libmath/sub/mod`, then `…/mod.oker` |

`oker_modules/` is searched in the importing file's directory, then each parent, and the
search stops at the project root, so code inside an installed package can still find its own
dependencies. Imports with `..` inside a package path, absolute paths, backslashes, or an
`oker.json` `main` that points outside the package are rejected.

Caching loaded modules and detecting circular imports remain the compiler's job.
Try the rules without touching the compiler: `opg resolve libmath --from src/main.oker`.

## Rules worth knowing

- Versions are immutable: publishing `name@1.0.0` twice is refused. Bump the version.
- The first user to publish a name owns it; only they can publish new versions.
- Dependencies are pinned to exact versions (`"libmath": "1.0.0"`).
- `oker_modules/` holds one copy of each package, so if two packages need different
  versions of the same dependency, `opg install` stops with a clear conflict message.
- Every download is checked against the registry's SHA-256 **before** it is unpacked, and
  the unpacker rejects path traversal, absolute paths, symlinks and oversize archives.
- `.okerignore` (one pattern per line: `*.log`, `build/`, `docs/`) controls what `pack` and
  `publish` leave out. `.git`, `oker_modules`, `.DS_Store` and `*.tar.gz` are always skipped.
- Pass the token with `OPG_TOKEN` rather than `--token` (command lines end up in shell history).
  `opg` warns if you send a token over plain `http://` to a non-local host.

## Registry API

```
GET  /api/packages/<name>                       versions and latest
GET  /api/packages/<name>/<version>             metadata: sha256, size, dependencies
GET  /api/packages/<name>/<version>/download    the .tar.gz
PUT  /api/packages/<name>/<version>             publish (Authorization: Bearer <token>)
```
