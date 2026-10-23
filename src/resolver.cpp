#include "resolver.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace opg {

namespace {

constexpr const char* kModules = "oker_modules";
constexpr const char* kManifest = "oker.json";
constexpr const char* kExt = ".oker";
constexpr const char* kDefaultMain = "src/main.oker";

bool plainName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    if (!(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

// "a/b/c": every segment non-empty, not "." or "..", no backslash or NUL.
bool safeSubpath(const std::string& s) {
    if (s.empty() || s.find('\\') != std::string::npos ||
        s.find('\0') != std::string::npos || s.front() == '/' || s.back() == '/') {
        return false;
    }
    size_t start = 0;
    while (true) {
        size_t slash = s.find('/', start);
        std::string part = s.substr(start, slash == std::string::npos ? slash : slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string::npos) return true;
        start = slash + 1;
    }
}

// <base>, then <base>.oker; records what was checked.
bool tryFile(const fs::path& base, ResolveResult& r) {
    std::error_code ec;
    for (const fs::path& candidate : {base, fs::path(base.string() + kExt)}) {
        r.tried.push_back(candidate);
        if (fs::is_regular_file(candidate, ec)) {
            r.found = true;
            r.path = candidate;
            return true;
        }
    }
    return false;
}

// "main" from <pkgDir>/oker.json, or the default. Returns "" if the value is unusable.
std::string packageMain(const fs::path& pkgDir) {
    std::ifstream in(pkgDir / kManifest);
    if (!in) return kDefaultMain;
    try {
        auto j = nlohmann::json::parse(in);
        if (j.is_object() && j.contains("main") && j["main"].is_string()) {
            std::string m = j["main"].get<std::string>();
            return safeSubpath(m) ? m : std::string();
        }
    } catch (...) {
    }
    return kDefaultMain;
}

ResolveResult fail(ResolveResult r, std::string message) {
    r.found = false;
    r.error = std::move(message);
    return r;
}

}  // namespace

ResolveResult resolveImport(const std::string& spec, const fs::path& importingFile) {
    ResolveResult r;
    if (spec.empty()) return fail(r, "empty import");

    const fs::path fromDir = fs::absolute(importingFile).lexically_normal().parent_path();

    // ---- relative imports -------------------------------------------------
    if (spec.rfind("./", 0) == 0 || spec.rfind("../", 0) == 0) {
        if (spec.find('\\') != std::string::npos) return fail(r, "backslash in import path");
        if (tryFile((fromDir / spec).lexically_normal(), r)) return r;
        return fail(r, "cannot find module '" + spec + "'");
    }
    if (spec.front() == '/' || spec.find('\\') != std::string::npos) {
        return fail(r, "absolute and backslash paths are not valid imports: '" + spec + "'");
    }

    // ---- package imports --------------------------------------------------
    const size_t slash = spec.find('/');
    const std::string name = spec.substr(0, slash);
    const std::string rest = (slash == std::string::npos) ? "" : spec.substr(slash + 1);

    if (!plainName(name)) return fail(r, "invalid package name in import '" + spec + "'");
    if (slash != std::string::npos && !safeSubpath(rest)) {
        return fail(r, "invalid path in import '" + spec + "'");
    }

    std::error_code ec;
    fs::path dir = fromDir;
    for (;;) {
        const fs::path pkgDir = dir / kModules / name;
        if (fs::is_directory(pkgDir, ec)) {
            if (rest.empty()) {
                const std::string mainRel = packageMain(pkgDir);
                if (mainRel.empty()) {
                    return fail(r, "package '" + name + "' has an invalid \"main\" in oker.json");
                }
                const fs::path mainPath = (pkgDir / mainRel).lexically_normal();
                r.tried.push_back(mainPath);
                if (fs::is_regular_file(mainPath, ec)) {
                    r.found = true;
                    r.path = mainPath;
                    return r;
                }
                return fail(r, "package '" + name + "' is installed but its entry point '" +
                                   mainRel + "' is missing");
            }
            if (tryFile((pkgDir / rest).lexically_normal(), r)) return r;
            return fail(r, "package '" + name + "' has no module '" + rest + "'");
        }

        // The project root ends the search. An oker.json inside oker_modules/
        // belongs to an installed package, not to the project.
        const bool hasManifest = fs::is_regular_file(dir / kManifest, ec);
        if (hasManifest && dir.parent_path().filename() != kModules) break;

        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return fail(r, "cannot find package '" + name + "' (try: opg install " + name + ")");
}

}  // namespace opg
