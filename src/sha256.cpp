#include "sha256.hpp"

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>

#include <openssl/evp.h>

namespace opg {

namespace {

struct CtxDeleter {
    void operator()(EVP_MD_CTX* c) const { EVP_MD_CTX_free(c); }
};
using Ctx = std::unique_ptr<EVP_MD_CTX, CtxDeleter>;

Ctx beginDigest() {
    Ctx ctx(EVP_MD_CTX_new());
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("failed to initialise SHA-256");
    }
    return ctx;
}

void update(Ctx& ctx, const void* data, size_t len) {
    if (EVP_DigestUpdate(ctx.get(), data, len) != 1) {
        throw std::runtime_error("SHA-256 update failed");
    }
}

std::string finish(Ctx& ctx) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> md{};
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx.get(), md.data(), &len) != 1) {
        throw std::runtime_error("SHA-256 finalisation failed");
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(hex[md[i] >> 4]);
        out.push_back(hex[md[i] & 0x0f]);
    }
    return out;
}

}  // namespace

std::string sha256File(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file.string());

    Ctx ctx = beginDigest();
    std::array<char, 64 * 1024> buf;
    while (in) {
        in.read(buf.data(), buf.size());
        std::streamsize n = in.gcount();
        if (n > 0) update(ctx, buf.data(), static_cast<size_t>(n));
    }
    if (in.bad()) throw std::runtime_error("read error on " + file.string());
    return finish(ctx);
}

std::string sha256String(const std::string& data) {
    Ctx ctx = beginDigest();
    update(ctx, data.data(), data.size());
    return finish(ctx);
}

}  // namespace opg
