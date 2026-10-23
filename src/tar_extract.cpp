#include "tar_extract.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include <zlib.h>

#include "manifest.hpp"

namespace fs = std::filesystem;

namespace opg {

namespace {

constexpr size_t kBlock = 512;

class GzReader {
public:
    explicit GzReader(const fs::path& path) {
        gz_ = gzopen(path.string().c_str(), "rb");
        if (!gz_) throw std::runtime_error("cannot open " + path.string());
    }
    ~GzReader() {
        if (gz_) gzclose(gz_);
    }
    GzReader(const GzReader&) = delete;
    GzReader& operator=(const GzReader&) = delete;

    // Reads up to n bytes; returns fewer only at end of data. Throws on a
    // corrupt gzip stream.
    size_t read(void* buf, size_t n) {
        size_t got = 0;
        while (got < n) {
            unsigned chunk = static_cast<unsigned>(std::min<size_t>(n - got, 1u << 30));
            int r = gzread(gz_, static_cast<char*>(buf) + got, chunk);
            if (r < 0) {
                int errnum = 0;
                const char* msg = gzerror(gz_, &errnum);
                throw std::runtime_error(std::string("corrupt archive: ") +
                                         (msg ? msg : "gzip error"));
            }
            if (r == 0) break;
            got += static_cast<size_t>(r);
        }
        return got;
    }

    void skip(std::uintmax_t n) {
        std::array<char, 8192> sink;
        while (n > 0) {
            size_t want = static_cast<size_t>(std::min<std::uintmax_t>(n, sink.size()));
            if (read(sink.data(), want) != want) {
                throw std::runtime_error("corrupt archive: truncated");
            }
            n -= want;
        }
    }

private:
    gzFile gz_ = nullptr;
};

bool allZero(const char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != 0) return false;
    }
    return true;
}

// Parses an octal header field (digits, optionally space/NUL padded).
std::uintmax_t parseOctal(const char* field, size_t width, const char* what) {
    if (static_cast<unsigned char>(field[0]) & 0x80) {
        throw std::runtime_error(std::string("unsupported archive: binary number in ") + what);
    }
    std::uintmax_t v = 0;
    size_t i = 0;
    while (i < width && field[i] == ' ') ++i;
    bool any = false;
    for (; i < width && field[i] >= '0' && field[i] <= '7'; ++i) {
        v = v * 8 + static_cast<std::uintmax_t>(field[i] - '0');
        any = true;
        if (v > (std::uintmax_t{1} << 60)) {
            throw std::runtime_error(std::string("corrupt archive: huge value in ") + what);
        }
    }
    for (; i < width; ++i) {
        if (field[i] != ' ' && field[i] != '\0') {
            throw std::runtime_error(std::string("corrupt archive: bad ") + what);
        }
    }
    if (!any) throw std::runtime_error(std::string("corrupt archive: empty ") + what);
    return v;
}

std::string cString(const char* p, size_t max) {
    return std::string(p, strnlen(p, max));
}

// Validates an archive path and returns it relative to "package/".
// Returns "" for the "package" directory itself.
std::string safeRelativePath(const std::string& full, bool isDir) {
    if (full.empty() || full.find('\\') != std::string::npos || full.front() == '/') {
        throw std::runtime_error("unsafe path in archive: '" + full + "'");
    }
    std::string path = full;
    if (path.back() == '/') {
        if (!isDir) throw std::runtime_error("unsafe path in archive: '" + full + "'");
        path.pop_back();
    }

    // Every component must be a plain name.
    size_t start = 0;
    std::string firstPart;
    while (true) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? slash : slash - start);
        if (part.empty() || part == "." || part == "..") {
            throw std::runtime_error("unsafe path in archive: '" + full + "'");
        }
        if (start == 0) firstPart = part;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }

    if (firstPart != "package") {
        throw std::runtime_error("unexpected entry outside package/: '" + full + "'");
    }
    if (path == "package") {
        if (!isDir) throw std::runtime_error("unsafe path in archive: '" + full + "'");
        return "";
    }
    return path.substr(sizeof("package/") - 1);
}

}  // namespace

void extractPackage(const fs::path& archive, const fs::path& destDir,
                    const ExtractLimits& limits) {
    GzReader gz(archive);

    std::uintmax_t totalBytes = 0;
    std::size_t entries = 0;
    std::array<char, 64 * 1024> buf;

    for (;;) {
        char h[kBlock];
        size_t n = gz.read(h, kBlock);
        if (n == 0) break;  // clean end of data
        if (n != kBlock) throw std::runtime_error("corrupt archive: truncated header");
        if (allZero(h, kBlock)) break;  // end-of-archive marker

        // Header checksum: sum of all bytes with the checksum field as spaces.
        {
            std::uintmax_t stored = parseOctal(h + 148, 8, "checksum");
            unsigned sum = 0;
            for (size_t i = 0; i < kBlock; ++i) {
                sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(h[i]);
            }
            if (stored != sum) throw std::runtime_error("corrupt archive: bad header checksum");
        }

        if (++entries > limits.maxEntries) {
            throw std::runtime_error("archive has too many entries");
        }

        std::string name = cString(h + 0, 100);
        std::string prefix = cString(h + 345, 155);
        const std::string fullName = prefix.empty() ? name : prefix + "/" + name;
        const char type = h[156];
        const std::uintmax_t size = parseOctal(h + 124, 12, "size");
        const std::uintmax_t mode = parseOctal(h + 100, 8, "mode");

        const bool isFile = (type == '0' || type == '\0');
        const bool isDir = (type == '5');
        if (!isFile && !isDir) {
            throw std::runtime_error(std::string("unsupported entry type '") +
                                     (std::isprint(static_cast<unsigned char>(type)) ? type : '?') +
                                     "' for '" + fullName +
                                     "' (only regular files and directories are allowed)");
        }
        if (isDir && size != 0) throw std::runtime_error("corrupt archive: directory with data");

        const std::string rel = safeRelativePath(fullName, isDir);

        if (isDir) {
            if (!rel.empty()) fs::create_directories(destDir / rel);
            continue;
        }
        if (rel.empty()) throw std::runtime_error("unsafe path in archive: '" + fullName + "'");

        totalBytes += size;
        if (totalBytes > limits.maxTotalBytes) {
            throw std::runtime_error("archive is too large when unpacked");
        }

        const fs::path target = destDir / rel;
        if (fs::exists(fs::symlink_status(target))) {
            throw std::runtime_error("duplicate entry in archive: '" + rel + "'");
        }
        fs::create_directories(target.parent_path());

        {
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("cannot create " + target.string());
            std::uintmax_t left = size;
            while (left > 0) {
                size_t want = static_cast<size_t>(std::min<std::uintmax_t>(left, buf.size()));
                if (gz.read(buf.data(), want) != want) {
                    throw std::runtime_error("corrupt archive: truncated file data");
                }
                out.write(buf.data(), static_cast<std::streamsize>(want));
                if (!out) throw std::runtime_error("write failed for " + target.string());
                left -= want;
            }
        }
        gz.skip((kBlock - (size % kBlock)) % kBlock);

        const bool exec = (mode & 0100) != 0;
        fs::permissions(target,
                        exec ? (fs::perms::owner_all | fs::perms::group_read |
                                fs::perms::group_exec | fs::perms::others_read |
                                fs::perms::others_exec)
                             : (fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::group_read | fs::perms::others_read),
                        fs::perm_options::replace);
    }

    if (!fs::is_regular_file(destDir / kManifestFile)) {
        throw std::runtime_error("package has no oker.json");
    }
}

}  // namespace opg
