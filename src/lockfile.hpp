#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace opg {

// 64 lowercase hex characters.
bool isValidSha256(const std::string& s);

struct LockedPackage {
    std::string version;
    std::string sha256;  // hash of the package archive, as published
    std::map<std::string, std::string> dependencies;  // name -> exact version

    bool operator==(const LockedPackage& o) const {
        return version == o.version && sha256 == o.sha256 && dependencies == o.dependencies;
    }
};

// oker.lock: the complete, exact set of packages an install produced.
//
//   {
//     "lockfileVersion": 1,
//     "packages": {
//       "libmath": { "version": "1.0.0", "sha256": "...", "dependencies": {} }
//     }
//   }
//
// Keys are sorted so the file diffs cleanly in version control.
class Lockfile {
public:
    std::map<std::string, LockedPackage> packages;

    // Throws std::runtime_error with a readable message if the file is
    // missing, unparsable, or fails validation.
    static Lockfile load(const std::filesystem::path& file);

    // Canonical text form (what save() writes).
    std::string toJson() const;

    // Atomic write: temp file + rename.
    void save(const std::filesystem::path& file) const;

    // True if this lock is exactly the dependency closure of `rootDeps`:
    // every root dependency is locked at the same version, every locked
    // dependency's own dependencies are locked at the versions it names, and
    // nothing unreachable is left over.
    bool covers(const std::map<std::string, std::string>& rootDeps) const;
};

}  // namespace opg
