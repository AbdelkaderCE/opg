#include "registry.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

namespace fs = std::filesystem;

namespace opg {

namespace {

std::string fromEnv(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string stripTrailingSlashes(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// Host part of "scheme://host[:port][/...]".
std::string hostOf(const std::string& url) {
    auto start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    auto end = url.find('/', start);
    std::string hostPort = url.substr(start, end == std::string::npos ? end : end - start);
    if (!hostPort.empty() && hostPort.front() == '[') {  // [::1]:4873
        auto close = hostPort.find(']');
        return hostPort.substr(0, close == std::string::npos ? close : close + 1);
    }
    return hostPort.substr(0, hostPort.find(':'));
}

bool isLoopback(const std::string& host) {
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

// Pulls {"error": "..."} out of a failed response, falling back to the status.
std::string errorMessage(int status, const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        if (j.is_object() && j.contains("error") && j["error"].is_string()) {
            return j["error"].get<std::string>();
        }
    } catch (...) {
    }
    return "registry returned HTTP " + std::to_string(status);
}

}  // namespace

std::string resolveRegistryUrl(const std::string& flagValue) {
    if (!flagValue.empty()) return flagValue;
    std::string env = fromEnv("OPG_REGISTRY");
    return env.empty() ? std::string(kDefaultRegistry) : env;
}

std::string resolveToken(const std::string& flagValue) {
    return flagValue.empty() ? fromEnv("OPG_TOKEN") : flagValue;
}

struct RegistryClient::Impl {
    explicit Impl(const std::string& url) : cli(url) {
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(60, 0);
        cli.set_write_timeout(60, 0);
        cli.set_follow_location(false);  // never forward the token to another host
    }
    httplib::Client cli;
};

RegistryClient::RegistryClient(const std::string& baseUrl, const std::string& token)
    : baseUrl_(stripTrailingSlashes(baseUrl)), token_(token) {
    const bool http = baseUrl_.rfind("http://", 0) == 0;
    const bool https = baseUrl_.rfind("https://", 0) == 0;
    if (!http && !https) {
        throw std::runtime_error("registry URL must start with http:// or https:// (got '" +
                                 baseUrl + "')");
    }
    if (http && !token_.empty() && !isLoopback(hostOf(baseUrl_))) {
        std::cerr << "warning: sending your token over plain http to " << hostOf(baseUrl_)
                  << "; use https to keep it private\n";
    }
    impl_ = std::make_unique<Impl>(baseUrl_);
}

RegistryClient::~RegistryClient() = default;

nlohmann::json RegistryClient::getJson(const std::string& path) {
    httplib::Headers headers{{"Accept", "application/json"}};
    auto res = impl_->cli.Get(path, headers);
    if (!res) {
        throw RegistryError(0, "cannot reach registry at " + baseUrl_ + " (" +
                                   httplib::to_string(res.error()) + ")");
    }
    if (res->status != 200) {
        throw RegistryError(res->status, errorMessage(res->status, res->body));
    }
    try {
        return nlohmann::json::parse(res->body);
    } catch (const nlohmann::json::exception&) {
        throw RegistryError(res->status, "registry sent an invalid JSON reply");
    }
}

void RegistryClient::download(const std::string& path, const fs::path& outFile,
                              std::uintmax_t maxBytes) {
    std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + outFile.string());

    int status = 0;
    std::uintmax_t total = 0;
    bool tooBig = false;
    std::string errBody;  // body of a non-200 reply (a small JSON error)

    auto res = impl_->cli.Get(
        path, httplib::Headers{},
        [&](const httplib::Response& r) {
            status = r.status;
            return true;
        },
        [&](const char* data, size_t len) {
            if (status != 200) {
                if (errBody.size() < 64 * 1024) errBody.append(data, len);
                return true;
            }
            total += len;
            if (total > maxBytes) {
                tooBig = true;
                return false;
            }
            out.write(data, static_cast<std::streamsize>(len));
            return static_cast<bool>(out);
        });
    out.close();

    auto fail = [&](RegistryError e) {
        std::error_code ec;
        fs::remove(outFile, ec);
        throw e;
    };

    if (tooBig) fail(RegistryError(status, "package is larger than the allowed download size"));
    if (!res) {
        fail(RegistryError(0, "download failed (" + httplib::to_string(res.error()) + ")"));
    }
    if (status != 200) fail(RegistryError(status, errorMessage(status, errBody)));
}

nlohmann::json RegistryClient::putArchive(const std::string& path, const std::string& body,
                                          const std::string& sha256) {
    httplib::Headers headers{{"Authorization", "Bearer " + token_},
                             {"X-Opg-Sha256", sha256},
                             {"Accept", "application/json"}};
    auto res = impl_->cli.Put(path, headers, body, "application/gzip");
    if (!res) {
        throw RegistryError(0, "cannot reach registry at " + baseUrl_ + " (" +
                                   httplib::to_string(res.error()) + ")");
    }
    if (res->status < 200 || res->status > 299) {
        throw RegistryError(res->status, errorMessage(res->status, res->body));
    }
    try {
        return nlohmann::json::parse(res->body);
    } catch (const nlohmann::json::exception&) {
        return nlohmann::json::object();
    }
}

}  // namespace opg
