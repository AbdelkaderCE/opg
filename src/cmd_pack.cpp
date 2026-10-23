#include <filesystem>
#include <iostream>
#include <string>

#include "commands.hpp"
#include "manifest.hpp"
#include "pack.hpp"

namespace fs = std::filesystem;

namespace opg {

int cmdPack(const PackOptions& opts) {
    const fs::path root = fs::current_path();
    const fs::path manifestPath = root / kManifestFile;

    if (!fs::exists(manifestPath)) {
        std::cerr << "error: no " << kManifestFile
                  << " in this directory (run 'opg init' first)\n";
        return 1;
    }

    try {
        const Manifest m = Manifest::load(manifestPath);

        const std::string archiveName = m.name + "-" + m.version + ".tar.gz";
        fs::path output = opts.output.empty() ? root / archiveName : fs::path(opts.output);
        if (fs::is_directory(output)) output /= archiveName;
        output = fs::absolute(output);

        const auto files = collectPackageFiles(root, output);
        warnIfEntryPointMissing(m, files);

        std::uintmax_t total = 0;
        std::cout << "opg pack: " << m.name << "@" << m.version << "\n\n";
        for (const auto& e : files) {
            std::cout << "  " << e.rel << "  (" << humanSize(e.size) << ")\n";
            total += e.size;
        }
        std::cout << "\n" << files.size() << " file(s), " << humanSize(total)
                  << " unpacked\n";

        if (opts.dryRun) {
            std::cout << "(dry run: no archive written)\n";
            return 0;
        }

        const PackResult r = writePackage(output, files);
        std::cout << "\nCreated " << output.filename().string() << "\n"
                  << "  size:   " << humanSize(r.archiveSize) << "\n"
                  << "  sha256: " << r.sha256 << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

}  // namespace opg
