#include "archive.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <zlib.h>

namespace fs = std::filesystem;

namespace opg {

namespace {

constexpr size_t kBlock = 512;

class GzWriter {
public:
    explicit GzWriter(const fs::path& path) {
        gz_ = gzopen(path.string().c_str(), "wb9");
        if (!gz_) throw std::runtime_error("cannot create " + path.string());
    }
    ~GzWriter() {
        if (gz_) gzclose(gz_);
    }
    GzWriter(const GzWriter&) = delete;
    GzWriter& operator=(const GzWriter&) = delete;

    void write(const void* data, size_t len) {
        while (len > 0) {
            unsigned chunk = len > (1u << 30) ? (1u << 30) : static_cast<unsigned>(len);
            int n = gzwrite(gz_, data, chunk);
            if (n <= 0) throw std::runtime_error("failed writing compressed data");
            data = static_cast<const char*>(data) + n;
            len -= static_cast<size_t>(n);
        }
    }

    void close() {
        int rc = gzclose(gz_);
        gz_ = nullptr;
        if (rc != Z_OK) throw std::runtime_error("failed finalising archive");
    }

private:
    gzFile gz_ = nullptr;
};

// Writes `value` as zero-padded octal into a field of `width` bytes
// (width-1 digits followed by NUL).
void putOctal(char* field, size_t width, std::uintmax_t value) {
    std::uintmax_t max = 1;
    for (size_t i = 0; i + 1 < width; ++i) max *= 8;
    if (value >= max) throw std::runtime_error("file too large for tar format");
    for (size_t i = width - 1; i-- > 0;) {
        field[i] = static_cast<char>('0' + (value & 7));
        value >>= 3;
    }
    field[width - 1] = '\0';
}

// ustar splits long paths into prefix (<=155) and name (<=100) at a '/'.
void splitPath(const std::string& full, std::string& prefix, std::string& name) {
    if (full.size() <= 100) {
        prefix.clear();
        name = full;
        return;
    }
    for (size_t pos = full.rfind('/'); pos != std::string::npos && pos > 0;
         pos = full.rfind('/', pos - 1)) {
        if (pos <= 155 && full.size() - pos - 1 <= 100) {
            prefix = full.substr(0, pos);
            name = full.substr(pos + 1);
            return;
        }
        if (pos == 0) break;
    }
    throw std::runtime_error("path too long for tar format: " + full);
}

void makeHeader(char* h, const std::string& fullPath, std::uintmax_t size, bool exec) {
    std::memset(h, 0, kBlock);
    std::string prefix, name;
    splitPath(fullPath, prefix, name);

    std::memcpy(h + 0, name.data(), name.size());
    putOctal(h + 100, 8, exec ? 0755 : 0644);
    putOctal(h + 108, 8, 0);     // uid
    putOctal(h + 116, 8, 0);     // gid
    putOctal(h + 124, 12, size);
    putOctal(h + 136, 12, 0);    // mtime
    std::memset(h + 148, ' ', 8);  // checksum placeholder
    h[156] = '0';                // regular file
    std::memcpy(h + 257, "ustar", 5);  // magic "ustar\0"
    std::memcpy(h + 263, "00", 2);     // version
    std::memcpy(h + 345, prefix.data(), prefix.size());

    unsigned sum = 0;
    for (size_t i = 0; i < kBlock; ++i) sum += static_cast<unsigned char>(h[i]);
    // 6 octal digits, NUL, space
    for (int i = 5; i >= 0; --i) {
        h[148 + i] = static_cast<char>('0' + (sum & 7));
        sum >>= 3;
    }
    h[154] = '\0';
    h[155] = ' ';
}

void writeFileBody(GzWriter& gz, const fs::path& file, std::uintmax_t expected) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + file.string());

    std::array<char, 64 * 1024> buf;
    std::uintmax_t total = 0;
    while (in) {
        in.read(buf.data(), buf.size());
        std::streamsize n = in.gcount();
        if (n > 0) {
            total += static_cast<std::uintmax_t>(n);
            if (total > expected) break;
            gz.write(buf.data(), static_cast<size_t>(n));
        }
    }
    if (in.bad() || total != expected) {
        throw std::runtime_error(file.string() + " changed while it was being packed");
    }

    size_t pad = (kBlock - (expected % kBlock)) % kBlock;
    if (pad) {
        static const char zeros[kBlock] = {};
        gz.write(zeros, pad);
    }
}

}  // namespace

void writeTarGz(const fs::path& out, const std::string& prefix,
                const std::vector<ArchiveEntry>& entries) {
    try {
        GzWriter gz(out);
        char header[kBlock];
        for (const ArchiveEntry& e : entries) {
            makeHeader(header, prefix + "/" + e.rel, e.size, e.executable);
            gz.write(header, kBlock);
            if (e.size > 0) writeFileBody(gz, e.absolute, e.size);
        }
        // End-of-archive marker: two zero blocks.
        static const char zeros[kBlock * 2] = {};
        gz.write(zeros, sizeof zeros);
        gz.close();
    } catch (...) {
        std::error_code ec;
        fs::remove(out, ec);
        throw;
    }
}

}  // namespace opg
