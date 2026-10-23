#include "lockfile.hpp"

#include <unistd.h>

#include <deque>
#include <fstream>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "manifest.hpp"

namespace fs = std::filesystem;

namespace opg {

using ordered_json = nlohmann::ordered_json;

bool isValidSha256(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

Lockfile Lockfile::load(const fs::path& file) {
    std::ifstream in(file);
    if (!in) throw std::runtime_error("cannot open " + file.string());

    auto bad = [&](const std::string& why) {
        throw std::runtime_error(file.string() + " is invalid (" + why +
                                 "). Delete it and run 'opg install' to regenerate it.");
    };

    ordered_json j;
    try {
        j = ordered_json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        bad(std::string("not valid JSON: ") + e.what());
    }
    if (!j.is_object()) bad("top-level value must be an object");
    if (!j.contains("lockfileVersion") || j["lockfileVersion"] != 1) {
        bad("unsupported lockfileVersion; this opg understands version 1");
    }
    if (!j.contains("packages") || !j["packages"].is_object()) bad("missing 'packages' object");

    Lockfile lock;
    for (auto it = j["packages"].begin(); it != j["packages"].end(); ++it) {
        const std::string& name = it.key();
        const auto& p = it.value();
        if (!isValidName(name)) bad("invalid package name '" + name + "'");
        if (!p.is_object()) bad("entry for '" + name + "' must be an object");

        LockedPackage lp;
        if (!p.contains("version") || !p["version"].is_string() ||
            !isValidVersion(p["version"].get<std::string>())) {
            bad("bad version for '" + name + "'");
        }
        lp.version = p["version"].get<std::string>();
        if (!p.contains("sha256") || !p["sha256"].is_string() ||
            !isValidSha256(p["sha256"].get<std::string>())) {
            bad("bad sha256 for '" + name + "'");
        }
        lp.sha256 = p["sha256"].get<std::string>();

        if (p.contains("dependencies")) {
            if (!p["dependencies"].is_object()) bad("'dependencies' of '" + name + "' is invalid");
            for (auto d = p["dependencies"].begin(); d != p["dependencies"].end(); ++d) {
                if (!isValidName(d.key()) || !d.value().is_string() ||
                    !isValidVersion(d.value().get<std::string>())) {
                    bad("bad dependency '" + d.key() + "' of '" + name + "'");
                }
                lp.dependencies[d.key()] = d.value().get<std::string>();
            }
        }
        lock.packages.emplace(name, std::move(lp));
    }
    return lock;
}

std::string Lockfile::toJson() const {
    ordered_json j;
    j["lockfileVersion"] = 1;
    j["packages"] = ordered_json::object();
    for (const auto& [name, p] : packages) {  // std::map: already sorted
        ordered_json e;
        e["version"] = p.version;
        e["sha256"] = p.sha256;
        e["dependencies"] = ordered_json::object();
        for (const auto& [dep, ver] : p.dependencies) e["dependencies"][dep] = ver;
        j["packages"][name] = std::move(e);
    }
    return j.dump(2) + "\n";
}

void Lockfile::save(const fs::path& file) const {
    const fs::path tmp = file.string() + ".tmp-" + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out << toJson();
        out.flush();
        if (!out) {
            std::error_code ec;
            fs::remove(tmp, ec);
            throw std::runtime_error("write failed for " + tmp.string());
        }
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error("cannot replace " + file.string());
    }
}

bool Lockfile::covers(const std::map<std::string, std::string>& rootDeps) const {
    std::set<std::string> reached;
    std::deque<std::string> queue;

    for (const auto& [name, version] : rootDeps) {
        auto it = packages.find(name);
        if (it == packages.end() || it->second.version != version) return false;
        if (reached.insert(name).second) queue.push_back(name);
    }
    while (!queue.empty()) {
        const std::string name = queue.front();
        queue.pop_front();
        for (const auto& [dep, version] : packages.at(name).dependencies) {
            auto it = packages.find(dep);
            if (it == packages.end() || it->second.version != version) return false;
            if (reached.insert(dep).second) queue.push_back(dep);
        }
    }
    return reached.size() == packages.size();
}

}  // namespace opg
