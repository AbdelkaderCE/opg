#include <filesystem>
#include <iostream>
#include <string>

#include "commands.hpp"
#include "manifest.hpp"

namespace fs = std::filesystem;

namespace opg {

// Prompts "label (default): " and returns the typed value, or the default if
// the line is empty. On EOF the default is returned.
static std::string ask(const std::string& label, const std::string& def) {
    std::cout << label;
    if (!def.empty()) std::cout << " (" << def << ")";
    std::cout << ": " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return def;
    // trim
    auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return def;
    auto e = line.find_last_not_of(" \t\r\n");
    return line.substr(b, e - b + 1);
}

// Keeps asking until validator accepts, unless non-interactive.
template <typename Validator>
static std::string askValid(const std::string& label, const std::string& def,
                            Validator ok, const std::string& hint) {
    for (;;) {
        std::string v = ask(label, def);
        if (ok(v)) return v;
        std::cerr << "  " << hint << "\n";
        if (!std::cin) return def;  // avoid an infinite loop on EOF
    }
}

int cmdInit(const InitOptions& opts) {
    const fs::path manifestPath = fs::current_path() / kManifestFile;

    if (fs::exists(manifestPath) && !opts.force) {
        std::cerr << "error: " << kManifestFile
                  << " already exists (use --force to overwrite)\n";
        return 1;
    }

    Manifest def;
    def.name = sanitizeName(fs::current_path().filename().string());

    Manifest m;
    m.name = opts.name.empty() ? def.name : opts.name;
    m.version = opts.version.empty() ? def.version : opts.version;
    m.description = opts.description;
    m.author = opts.author;
    m.license = opts.license.empty() ? def.license : opts.license;
    m.main = opts.main.empty() ? def.main : opts.main;

    if (!opts.yes) {
        std::cout << "This will create " << kManifestFile
                  << ". Press Enter to accept the default shown in parentheses.\n\n";
        if (opts.name.empty()) {
            m.name = askValid("package name", m.name, isValidName,
                              "name must be lowercase letters, digits, '-' or '_', "
                              "starting with a letter");
        }
        if (opts.version.empty()) {
            m.version = askValid("version", m.version, isValidVersion,
                                 "version must look like 1.2.3");
        }
        if (opts.description.empty()) m.description = ask("description", "");
        if (opts.main.empty()) m.main = ask("entry point", m.main);
        if (opts.author.empty()) m.author = ask("author", "");
        if (opts.license.empty()) m.license = ask("license", m.license);
        std::cout << '\n';
    }

    // Validate values coming from flags (prompts already validated theirs).
    if (!isValidName(m.name)) {
        std::cerr << "error: invalid package name '" << m.name
                  << "' (lowercase letters, digits, '-' or '_', starting with a letter)\n";
        return 1;
    }
    if (!isValidVersion(m.version)) {
        std::cerr << "error: invalid version '" << m.version
                  << "' (expected MAJOR.MINOR.PATCH)\n";
        return 1;
    }

    try {
        m.save(manifestPath);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    std::cout << "Created " << kManifestFile << " for '" << m.name << "@" << m.version
              << "'\n";
    return 0;
}

}  // namespace opg
