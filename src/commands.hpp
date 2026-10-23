#pragma once

#include <string>
#include <vector>

namespace opg {

struct InitOptions {
    std::string name;        // empty -> derive from directory name
    std::string version;     // empty -> default
    std::string description;
    std::string author;
    std::string license;
    std::string main;
    bool yes = false;        // accept defaults, no prompts
    bool force = false;      // overwrite an existing oker.json
};

struct PackOptions {
    std::string output;      // file or directory; empty -> ./<name>-<version>.tar.gz
    bool dryRun = false;     // list files only, write nothing
};

struct PublishOptions {
    std::string registry;    // empty -> $OPG_REGISTRY or the default
    std::string token;       // empty -> $OPG_TOKEN
    bool dryRun = false;     // build and show, but do not upload
};

struct InstallOptions {
    std::vector<std::string> packages;  // "name" or "name@1.2.3"; empty -> manifest deps
    std::string registry;               // empty -> $OPG_REGISTRY or the default
    bool noSave = false;                // don't touch oker.json or oker.lock
    bool force = false;                 // reinstall even if the version is present
    bool frozen = false;                // CI mode: require an up-to-date oker.lock, change nothing
};

struct ResolveOptions {
    std::string spec;        // the import string, e.g. "libmath" or "./util"
    std::string from;        // file containing the import (default: ./main.oker)
};

// Each command returns a process exit code.
int cmdInit(const InitOptions& opts);
int cmdPack(const PackOptions& opts);
int cmdPublish(const PublishOptions& opts);
int cmdInstall(const InstallOptions& opts);
int cmdResolve(const ResolveOptions& opts);

}  // namespace opg
