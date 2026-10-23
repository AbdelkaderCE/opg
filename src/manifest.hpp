#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace opg {

inline constexpr const char* kManifestFile = "oker.json";
inline constexpr const char* kLockFile = "oker.lock";
inline constexpr const char* kModulesDir = "oker_modules";

// In-memory representation of oker.json.
struct Manifest {
    std::string name;
    std::string version = "0.1.0";
    std::string description;
    std::string main = "src/main.oker";
    std::string author;
    std::string license = "MIT";
    // package name -> version requirement (exact version for now)
    std::map<std::string, std::string> dependencies;

    // Reads and validates a manifest. Throws std::runtime_error on failure.
    static Manifest load(const std::filesystem::path& file);

    // Writes the manifest as pretty-printed JSON (2-space indent).
    void save(const std::filesystem::path& file) const;
};

// Package names: lowercase letters, digits, '-' and '_'; must start with a
// letter; 1..64 chars.
bool isValidName(const std::string& name);

// Strict MAJOR.MINOR.PATCH with optional -prerelease suffix.
bool isValidVersion(const std::string& version);

// Turns an arbitrary directory name into a valid package name
// (lowercased, invalid chars -> '-'). Returns "my-package" if nothing is left.
std::string sanitizeName(const std::string& raw);

}  // namespace opg
