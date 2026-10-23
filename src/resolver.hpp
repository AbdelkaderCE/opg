#pragma once

// Module resolver for Oker imports.
//
// Self-contained on purpose: it needs only <filesystem> and nlohmann/json, so
// the Oker compiler/VM can compile resolver.hpp + resolver.cpp directly,
// without linking anything else from opg.
//
// Rules (an import string is `spec`, the file containing the import is
// `importingFile`):
//
//   Relative   spec starts with "./" or "../"
//              -> resolved against the importing file's directory.
//              Tries <path> then <path>.oker.
//
//   Package    anything else: "name" or "name/sub/path"
//              -> looks for oker_modules/<name> in the importing file's
//              directory, then each parent directory, stopping after the
//              project root (the first directory with an oker.json that is not
//              itself an installed package).
//              "name"          -> the package's "main" from its oker.json
//                                 (default src/main.oker)
//              "name/sub/path" -> <package>/sub/path, then <package>/sub/path.oker
//
// Packages can never reach outside their own directory: ".." parts, absolute
// paths and backslashes in the import string are rejected, and the entry
// point named by oker.json must stay inside the package.

#include <filesystem>
#include <string>
#include <vector>

namespace opg {

struct ResolveResult {
    bool found = false;
    std::filesystem::path path;                 // set when found (absolute, normalised)
    std::string error;                          // set when not found
    std::vector<std::filesystem::path> tried;   // candidates checked, in order
};

ResolveResult resolveImport(const std::string& spec,
                            const std::filesystem::path& importingFile);

}  // namespace opg
