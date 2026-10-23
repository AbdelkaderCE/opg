#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "archive.hpp"
#include "manifest.hpp"

namespace opg {

struct PackResult {
    std::filesystem::path archive;
    std::string sha256;
    std::uintmax_t archiveSize = 0;
    std::uintmax_t unpackedSize = 0;
    std::size_t fileCount = 0;
};

// "1.5 kB" style size for messages.
std::string humanSize(std::uintmax_t bytes);

// Files that belong in the package (ignore rules applied), sorted by path.
// `outputAbs` is excluded so an archive written inside the project never
// packs itself. Symlinks are skipped with a warning. Throws on I/O errors.
std::vector<ArchiveEntry> collectPackageFiles(const std::filesystem::path& root,
                                              const std::filesystem::path& outputAbs);

// Prints a warning if the manifest's entry point is not among `files`.
void warnIfEntryPointMissing(const Manifest& m, const std::vector<ArchiveEntry>& files);

// Writes the reproducible .tar.gz and hashes it. Throws on failure.
PackResult writePackage(const std::filesystem::path& outputAbs,
                        const std::vector<ArchiveEntry>& files);

}  // namespace opg
