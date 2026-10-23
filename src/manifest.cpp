#include "manifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace opg {

using ordered_json = nlohmann::ordered_json;

bool isValidName(const std::string& name) {
    static const std::regex re("^[a-z][a-z0-9_-]{0,63}$");
    return std::regex_match(name, re);
}

bool isValidVersion(const std::string& version) {
    static const std::regex re(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$");
    return std::regex_match(version, re);
}

std::string sanitizeName(const std::string& raw) {
    std::string out;
    for (unsigned char c : raw) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == '-' || c == '_') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('-');
        }
    }
    // Must start with a letter.
    while (!out.empty() && !std::isalpha(static_cast<unsigned char>(out.front()))) {
        out.erase(out.begin());
    }
    if (out.size() > 64) out.resize(64);
    return out.empty() ? "my-package" : out;
}

static std::string getString(const ordered_json& j, const char* key,
                             const std::string& fallback) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null()) return fallback;
    if (!it->is_string()) {
        throw std::runtime_error(std::string("field '") + key + "' must be a string");
    }
    return it->get<std::string>();
}

Manifest Manifest::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        throw std::runtime_error("cannot open " + file.string());
    }

    ordered_json j;
    try {
        j = ordered_json::parse(in);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(file.string() + ": invalid JSON (" + e.what() + ")");
    }
    if (!j.is_object()) {
        throw std::runtime_error(file.string() + ": top-level value must be an object");
    }

    Manifest m;
    Manifest defaults;
    m.name = getString(j, "name", "");
    m.version = getString(j, "version", defaults.version);
    m.description = getString(j, "description", "");
    m.main = getString(j, "main", defaults.main);
    m.author = getString(j, "author", "");
    m.license = getString(j, "license", defaults.license);

    if (!isValidName(m.name)) {
        throw std::runtime_error(
            "invalid or missing 'name' (use lowercase letters, digits, '-' or '_', "
            "starting with a letter)");
    }
    if (!isValidVersion(m.version)) {
        throw std::runtime_error("invalid 'version' '" + m.version +
                                 "' (expected MAJOR.MINOR.PATCH)");
    }

    auto deps = j.find("dependencies");
    if (deps != j.end() && !deps->is_null()) {
        if (!deps->is_object()) {
            throw std::runtime_error("'dependencies' must be an object");
        }
        for (auto it = deps->begin(); it != deps->end(); ++it) {
            if (!isValidName(it.key())) {
                throw std::runtime_error("invalid dependency name '" + it.key() + "'");
            }
            if (!it.value().is_string()) {
                throw std::runtime_error("dependency '" + it.key() +
                                         "' must map to a version string");
            }
            m.dependencies[it.key()] = it.value().get<std::string>();
        }
    }
    return m;
}

void Manifest::save(const std::filesystem::path& file) const {
    ordered_json j;
    j["name"] = name;
    j["version"] = version;
    j["description"] = description;
    j["main"] = main;
    j["author"] = author;
    j["license"] = license;
    j["dependencies"] = ordered_json::object();
    for (const auto& [dep, req] : dependencies) {
        j["dependencies"][dep] = req;
    }

    std::ofstream out(file, std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write " + file.string());
    }
    out << j.dump(2) << '\n';
    if (!out) {
        throw std::runtime_error("write failed for " + file.string());
    }
}

}  // namespace opg
