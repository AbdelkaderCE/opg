#include <filesystem>
#include <iostream>

#include "commands.hpp"
#include "resolver.hpp"

namespace fs = std::filesystem;

namespace opg {

// Debug helper: shows exactly where an import would be loaded from.
// stdout gets only the path (easy to script); problems go to stderr.
int cmdResolve(const ResolveOptions& opts) {
    const fs::path from = opts.from.empty() ? fs::current_path() / "main.oker" : fs::path(opts.from);
    const ResolveResult r = resolveImport(opts.spec, from);

    if (r.found) {
        std::cout << r.path.string() << '\n';
        return 0;
    }
    std::cerr << "error: " << r.error << '\n';
    if (!r.tried.empty()) {
        std::cerr << "tried:\n";
        for (const auto& p : r.tried) std::cerr << "  " << p.string() << '\n';
    }
    return 1;
}

}  // namespace opg
