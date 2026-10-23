#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace opg {

struct ArchiveEntry {
    std::filesystem::path absolute;  // where to read the file from
    std::string rel;                 // path inside the archive (no prefix), '/' separators
    std::uintmax_t size = 0;
    bool executable = false;
};

// Writes a gzip-compressed ustar archive. Every entry is stored under
// "<prefix>/<rel>".
//
// The output is reproducible: entries are written in the order given, with
// mtime 0, uid/gid 0, no user/group names and a fixed mode (0644 or 0755),
// and zlib writes a zero timestamp in the gzip header. The same input files
// therefore always produce the same bytes, and the same SHA-256.
//
// Throws std::runtime_error on any failure (the partial output is removed).
void writeTarGz(const std::filesystem::path& out, const std::string& prefix,
                const std::vector<ArchiveEntry>& entries);

}  // namespace opg
