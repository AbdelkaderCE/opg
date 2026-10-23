#include "pack.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>

#include <sys/stat.h>

#include "ignore.hpp"
#include "sha256.hpp"

namespace fs = std::filesystem;

namespace opg {

std::string humanSize(std::uintmax_t bytes) {
    char buf[32];
    if (bytes < 1024) {
        std::snprintf(buf, sizeof buf, "%ju B", bytes);
    } else if (bytes < 1024ull * 1024) {
        std::snprintf(buf, sizeof buf, "%.1f kB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buf, sizeof buf, "%.2f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buf;
}

static bool isExecutable(const fs::path& p) {
    struct stat st{};
    if (::stat(p.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IXUSR) != 0;
}

std::vector<ArchiveEntry> collectPackageFiles(const fs::path& root,
                                              const fs::path& outputAbs) {
    const IgnoreRules rules = IgnoreRules::load(root);

    std::vector<ArchiveEntry> files;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied,
                                        ec),
        end;
    if (ec) throw std::runtime_error("cannot read " + root.string() + ": " + ec.message());

    for (; it != end; it.increment(ec)) {
        if (ec) throw std::runtime_error("directory scan failed: " + ec.message());

        const fs::path& p = it->path();
        std::string rel = p.lexically_relative(root).generic_string();

        bool isLink = it->is_symlink(ec);
        bool isDir = !isLink && it->is_directory(ec);

        if (rules.ignored(rel, isDir)) {
            if (isDir) it.disable_recursion_pending();
            continue;
        }
        if (isLink) {
            std::cerr << "warning: skipping symlink " << rel << '\n';
            continue;
        }
        if (isDir || !it->is_regular_file(ec)) continue;

        if (fs::equivalent(p, outputAbs, ec)) continue;  // never pack our own output

        ArchiveEntry e;
        e.absolute = p;
        e.rel = rel;
        e.size = it->file_size(ec);
        if (ec) throw std::runtime_error("cannot stat " + rel + ": " + ec.message());
        e.executable = isExecutable(p);
        files.push_back(std::move(e));
    }

    std::sort(files.begin(), files.end(),
              [](const ArchiveEntry& a, const ArchiveEntry& b) { return a.rel < b.rel; });
    return files;
}

void warnIfEntryPointMissing(const Manifest& m, const std::vector<ArchiveEntry>& files) {
    const std::string want = fs::path(m.main).lexically_normal().generic_string();
    bool found = std::any_of(files.begin(), files.end(),
                             [&](const ArchiveEntry& e) { return e.rel == want; });
    if (!found) {
        std::cerr << "warning: entry point '" << m.main
                  << "' not found in the package contents\n";
    }
}

PackResult writePackage(const fs::path& outputAbs, const std::vector<ArchiveEntry>& files) {
    PackResult r;
    r.archive = outputAbs;
    r.fileCount = files.size();
    for (const auto& e : files) r.unpackedSize += e.size;

    if (outputAbs.has_parent_path()) fs::create_directories(outputAbs.parent_path());
    writeTarGz(outputAbs, "package", files);
    r.archiveSize = fs::file_size(outputAbs);
    r.sha256 = sha256File(outputAbs);
    return r;
}

}  // namespace opg
