#include <unistd.h>

#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include "commands.hpp"
#include "lockfile.hpp"
#include "manifest.hpp"
#include "registry.hpp"
#include "sha256.hpp"
#include "tar_extract.hpp"

namespace fs = std::filesystem;

namespace opg {

namespace {

constexpr std::uintmax_t kMaxDownloadBytes = 100ull * 1024 * 1024;

struct Resolved {
    std::string version;
    std::string sha256;
    std::map<std::string, std::string> deps;
    std::string requiredBy;  // who asked for it first, for error messages
};

struct Request {
    std::string name;
    std::string version;
    std::string requiredBy;
};

// Everything the registry tells us is untrusted: names become directory
// names and versions become URL parts, so both are validated strictly.
Resolved fetchVersionInfo(RegistryClient& client, const Request& req) {
    nlohmann::json j = client.getJson("/api/packages/" + req.name + "/" + req.version);

    auto fail = [&](const std::string& why) {
        throw std::runtime_error("registry sent bad metadata for " + req.name + "@" +
                                 req.version + ": " + why);
    };
    if (!j.is_object()) fail("not an object");
    if (j.value("name", "") != req.name) fail("name mismatch");
    if (j.value("version", "") != req.version) fail("version mismatch");

    Resolved r;
    r.version = req.version;
    r.requiredBy = req.requiredBy;
    r.sha256 = j.value("sha256", "");
    if (!isValidSha256(r.sha256)) fail("invalid sha256");

    auto deps = j.find("dependencies");
    if (deps != j.end() && !deps->is_null()) {
        if (!deps->is_object()) fail("'dependencies' is not an object");
        for (auto it = deps->begin(); it != deps->end(); ++it) {
            if (!isValidName(it.key())) fail("invalid dependency name");
            if (!it.value().is_string() || !isValidVersion(it.value().get<std::string>())) {
                fail("invalid dependency version for '" + it.key() + "'");
            }
            r.deps[it.key()] = it.value().get<std::string>();
        }
    }
    return r;
}

std::string installedVersion(const fs::path& dir) {
    try {
        return Manifest::load(dir / kManifestFile).version;
    } catch (...) {
        return "";
    }
}

// Cleans up scratch files whether the install succeeds or throws.
struct Scratch {
    fs::path archive;
    fs::path staging;
    ~Scratch() {
        std::error_code ec;
        if (!archive.empty()) fs::remove(archive, ec);
        if (!staging.empty()) fs::remove_all(staging, ec);
    }
};

void installOne(RegistryClient& client, const fs::path& modules, const std::string& name,
                const Resolved& r) {
    const std::string tag = ".opg-" + name + "-" + std::to_string(::getpid());
    Scratch scratch{modules / (tag + ".tar.gz"), modules / (tag + ".staging")};

    client.download("/api/packages/" + name + "/" + r.version + "/download", scratch.archive,
                    kMaxDownloadBytes);

    // Verify BEFORE touching the archive contents.
    const std::string actual = sha256File(scratch.archive);
    if (actual != r.sha256) {
        throw std::runtime_error("checksum mismatch for " + name + "@" + r.version +
                                 "\n  expected " + r.sha256 + "\n  got      " + actual +
                                 "\nrefusing to install (the download is corrupted or has been "
                                 "tampered with)");
    }

    fs::create_directory(scratch.staging);
    extractPackage(scratch.archive, scratch.staging);

    // The package must be what the registry said it is.
    const Manifest inner = Manifest::load(scratch.staging / kManifestFile);
    if (inner.name != name || inner.version != r.version) {
        throw std::runtime_error("package contents do not match " + name + "@" + r.version +
                                 " (oker.json says " + inner.name + "@" + inner.version + ")");
    }

    const fs::path target = modules / name;
    std::error_code ec;
    fs::remove_all(target, ec);
    fs::rename(scratch.staging, target);
    scratch.staging.clear();  // moved into place: nothing left to clean
}

bool parseSpec(const std::string& spec, std::string& name, std::string& version) {
    auto at = spec.find('@');
    name = spec.substr(0, at);
    version = (at == std::string::npos) ? "" : spec.substr(at + 1);
    if (!isValidName(name)) return false;
    if (at != std::string::npos && !isValidVersion(version)) return false;
    return true;
}

// Breadth-first walk of the dependency tree using registry metadata. One copy
// of each package lives in oker_modules/, so all requests for a package must
// agree on the version. Throws std::runtime_error on conflict.
std::map<std::string, Resolved> resolveFromRegistry(
    RegistryClient& client, const std::map<std::string, std::string>& roots,
    const std::string& rootName) {
    std::map<std::string, Resolved> resolved;
    std::deque<Request> queue;
    for (const auto& [name, version] : roots) queue.push_back({name, version, rootName});

    while (!queue.empty()) {
        Request req = queue.front();
        queue.pop_front();

        auto seen = resolved.find(req.name);
        if (seen != resolved.end()) {
            if (seen->second.version != req.version) {
                throw std::runtime_error(
                    "version conflict for '" + req.name + "':\n  " + seen->second.requiredBy +
                    " needs " + req.name + "@" + seen->second.version + "\n  " + req.requiredBy +
                    " needs " + req.name + "@" + req.version +
                    "\nonly one version of a package can be installed.");
            }
            continue;
        }
        Resolved r = fetchVersionInfo(client, req);
        for (const auto& [dep, depVersion] : r.deps) {
            queue.push_back({dep, depVersion, req.name + "@" + req.version});
        }
        resolved.emplace(req.name, std::move(r));
    }
    return resolved;
}

}  // namespace

int cmdInstall(const InstallOptions& opts) {
    const fs::path root = fs::current_path();
    const fs::path manifestPath = root / kManifestFile;
    const fs::path lockPath = root / kLockFile;
    const fs::path modules = root / kModulesDir;

    if (!fs::exists(manifestPath)) {
        std::cerr << "error: no " << kManifestFile
                  << " in this directory (run 'opg init' first)\n";
        return 1;
    }
    if (opts.frozen && !opts.packages.empty()) {
        std::cerr << "error: --frozen cannot be combined with package names "
                     "(it never changes oker.json or oker.lock)\n";
        return 1;
    }

    try {
        Manifest manifest = Manifest::load(manifestPath);
        RegistryClient client(resolveRegistryUrl(opts.registry));

        std::optional<Lockfile> oldLock;
        if (fs::exists(lockPath)) oldLock = Lockfile::load(lockPath);

        // 1. Packages named on the command line: pin them to exact versions.
        std::map<std::string, std::string> wanted = manifest.dependencies;
        std::map<std::string, std::string> added;
        for (const std::string& spec : opts.packages) {
            std::string name, version;
            if (!parseSpec(spec, name, version)) {
                std::cerr << "error: invalid package spec '" << spec
                          << "' (use name or name@1.2.3)\n";
                return 1;
            }
            if (version.empty()) {
                nlohmann::json info = client.getJson("/api/packages/" + name);
                if (!info.contains("latest") || !info["latest"].is_string()) {
                    throw std::runtime_error("'" + name + "' has no published versions");
                }
                version = info["latest"].get<std::string>();
                if (!isValidVersion(version)) {
                    throw std::runtime_error("registry sent an invalid version for " + name);
                }
            }
            wanted[name] = version;
            added[name] = version;
        }

        if (wanted.empty()) {
            std::cout << "Nothing to install (no dependencies in " << kManifestFile << ").\n";
            return 0;
        }

        // 2. Decide where the exact package set comes from: the lockfile when
        //    it matches oker.json, otherwise the registry.
        std::map<std::string, Resolved> resolved;
        bool fromLock = false;

        if (added.empty() && oldLock && oldLock->covers(wanted)) {
            fromLock = true;
            for (const auto& [name, p] : oldLock->packages) {
                resolved[name] = Resolved{p.version, p.sha256, p.dependencies, ""};
            }
            std::cout << "Using " << kLockFile << "\n";
        } else {
            if (opts.frozen) {
                std::cerr << "error: "
                          << (oldLock ? std::string(kLockFile) + " does not match " + kManifestFile
                                      : std::string("there is no ") + kLockFile)
                          << " (--frozen refuses to change it).\n"
                          << "Run 'opg install' without --frozen, then commit " << kLockFile
                          << ".\n";
                return 1;
            }
            resolved = resolveFromRegistry(client, wanted, manifest.name);

            // A version that is already locked must still hash the same. If the
            // registry now reports something else, someone changed a published
            // package. Never paper over that by rewriting the lock.
            if (oldLock) {
                for (const auto& [name, r] : resolved) {
                    auto locked = oldLock->packages.find(name);
                    if (locked != oldLock->packages.end() &&
                        locked->second.version == r.version && locked->second.sha256 != r.sha256) {
                        throw std::runtime_error(
                            "registry hash for " + name + "@" + r.version + " differs from " +
                            kLockFile + "\n  locked   " + locked->second.sha256 +
                            "\n  registry " + r.sha256 +
                            "\nthe published package changed, which should never happen. "
                            "Refusing to continue.");
                        }
                }
            }
        }

        // 3. Download, verify and install.
        fs::create_directories(modules);
        std::size_t installed = 0, upToDate = 0;
        for (const auto& [name, r] : resolved) {
            if (!opts.force && installedVersion(modules / name) == r.version) {
                std::cout << "  = " << name << "@" << r.version << " (already installed)\n";
                ++upToDate;
                continue;
            }
            installOne(client, modules, name, r);
            std::cout << "  + " << name << "@" << r.version << "\n";
            ++installed;
        }

        // 4. Record the result, only now that everything worked.
        if (!opts.noSave && !opts.frozen) {
            if (!added.empty()) {
                for (const auto& [name, version] : added) manifest.dependencies[name] = version;
                manifest.save(manifestPath);
                std::cout << "\nUpdated " << kManifestFile << "\n";
            }
            if (!fromLock) {
                Lockfile lock;
                for (const auto& [name, r] : resolved) {
                    lock.packages[name] = LockedPackage{r.version, r.sha256, r.deps};
                }
                if (!oldLock || oldLock->toJson() != lock.toJson()) {
                    lock.save(lockPath);
                    std::cout << (added.empty() ? "\n" : "") << "Wrote " << kLockFile << "\n";
                }
            }
        }

        std::cout << "\n" << installed << " installed, " << upToDate << " up to date\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

}  // namespace opg
