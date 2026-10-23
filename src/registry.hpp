#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace opg {

inline constexpr const char* kDefaultRegistry = "http://localhost:4873";

// Registry address: explicit value > $OPG_REGISTRY > default.
std::string resolveRegistryUrl(const std::string& flagValue);

// Publish token: explicit value > $OPG_TOKEN > empty.
std::string resolveToken(const std::string& flagValue);

// Failure talking to the registry. `status` is the HTTP status, or 0 when the
// request never got a response (connection refused, timeout, TLS failure...).
class RegistryError : public std::runtime_error {
public:
    RegistryError(int status, const std::string& message)
        : std::runtime_error(message), status_(status) {}
    int status() const { return status_; }

private:
    int status_;
};

class RegistryClient {
public:
    // baseUrl must start with http:// or https://. The token, if any, is sent
    // as a Bearer token and only ever to this registry.
    explicit RegistryClient(const std::string& baseUrl, const std::string& token = "");
    ~RegistryClient();
    RegistryClient(const RegistryClient&) = delete;
    RegistryClient& operator=(const RegistryClient&) = delete;

    const std::string& baseUrl() const { return baseUrl_; }

    // GET <path> and parse the JSON body.
    nlohmann::json getJson(const std::string& path);

    // GET <path> straight into `outFile` (at most maxBytes). On any failure
    // the partial file is removed.
    void download(const std::string& path, const std::filesystem::path& outFile,
                  std::uintmax_t maxBytes);

    // PUT the archive bytes to <path>; returns the server's JSON reply.
    nlohmann::json putArchive(const std::string& path, const std::string& body,
                              const std::string& sha256);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string baseUrl_;
    std::string token_;
};

}  // namespace opg
