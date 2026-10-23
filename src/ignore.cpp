#include "ignore.hpp"

#include <fstream>

namespace opg {

bool globMatch(const std::string& pattern, const std::string& text) {
    // Iterative matcher with single-star backtracking.
    size_t p = 0, t = 0;
    size_t starP = std::string::npos, starT = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] != '*' &&
            (pattern[p] == text[t] || (pattern[p] == '?' && text[t] != '/'))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starT = t;
        } else if (starP != std::string::npos && text[starT] != '/') {
            p = starP + 1;
            t = ++starT;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

void IgnoreRules::add(const std::string& rawLine) {
    std::string line = rawLine;
    // trim whitespace / CR
    auto b = line.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return;
    auto e = line.find_last_not_of(" \t\r\n");
    line = line.substr(b, e - b + 1);
    if (line.empty() || line[0] == '#') return;

    Rule r;
    if (line.back() == '/') {
        r.dirOnly = true;
        line.pop_back();
    }
    if (!line.empty() && line.front() == '/') line.erase(line.begin());
    if (line.empty()) return;
    r.anchored = line.find('/') != std::string::npos;
    r.pattern = line;
    rules_.push_back(std::move(r));
}

IgnoreRules IgnoreRules::load(const std::filesystem::path& projectDir) {
    IgnoreRules ig;
    for (const char* builtin : {".git", "oker_modules", ".DS_Store", "*.tar.gz"}) {
        ig.add(builtin);
    }
    std::ifstream in(projectDir / ".okerignore");
    std::string line;
    while (in && std::getline(in, line)) ig.add(line);
    return ig;
}

bool IgnoreRules::ignored(const std::string& rel, bool isDir) const {
    std::string base = rel;
    auto slash = rel.find_last_of('/');
    if (slash != std::string::npos) base = rel.substr(slash + 1);

    for (const Rule& r : rules_) {
        if (r.dirOnly && !isDir) continue;
        if (globMatch(r.pattern, r.anchored ? rel : base)) return true;
    }
    return false;
}

}  // namespace opg
