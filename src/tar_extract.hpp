#pragma once

#include <cstdint>
#include <filesystem>

namespace opg {

struct ExtractLimits {
    std::uintmax_t maxTotalBytes = 512ull * 1024 * 1024;  // sum of all file sizes
    std::size_t maxEntries = 20000;
};

// Extracts a package archive (gzip'd ustar, everything under "package/") into
// `destDir`, which must exist and be empty. The "package/" prefix is removed.
//
// Archives come from the network, so this is strict instead of forgiving.
// It throws std::runtime_error (leaving partial output for the caller to
// delete) on:
//   - paths that are absolute, contain "..", ".", empty parts, or backslashes
//   - entries outside "package/"
//   - anything except regular files and directories (symlinks, hard links,
//     devices, pax/GNU extension headers)
//   - duplicate entries, bad header checksums, truncated data
//   - more entries or more bytes than `limits` allow
//   - a missing oker.json at the package root
//
// File modes are normalised to 0644, or 0755 if the owner-execute bit is set.
void extractPackage(const std::filesystem::path& archive, const std::filesystem::path& destDir,
                    const ExtractLimits& limits = {});

}  // namespace opg
