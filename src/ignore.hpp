#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace opg {

// Decides which files are left out of a package.
//
// Built-in rules always apply: .git, oker_modules, .DS_Store, *.tar.gz.
// A project can add its own in a .okerignore file (one pattern per line):
//   # comment
//   name        matches any file or directory called "name"
//   *.log       '*' and '?' wildcards (they never cross a '/')
//   docs/       trailing '/' matches directories only
//   src/tmp     a pattern containing '/' is matched against the full
//               project-relative path
class IgnoreRules {
public:
    // Built-in rules plus <projectDir>/.okerignore if it exists.
    static IgnoreRules load(const std::filesystem::path& projectDir);

    // rel uses '/' separators and is relative to the project root.
    bool ignored(const std::string& rel, bool isDir) const;

private:
    struct Rule {
        std::string pattern;
        bool dirOnly = false;
        bool anchored = false;  // pattern contains '/'
    };
    std::vector<Rule> rules_;

    void add(const std::string& line);
};

// '*' matches any run of characters except '/', '?' matches one character
// except '/'. Exposed for testing.
bool globMatch(const std::string& pattern, const std::string& text);

}  // namespace opg
