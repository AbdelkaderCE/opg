#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "commands.hpp"
#include "manifest.hpp"
#include "pack.hpp"
#include "registry.hpp"

namespace fs = std::filesystem;

namespace opg {

namespace {

// Removes the temporary archive however we leave the function.
struct TempFile {
    fs::path path;
    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

}  // namespace

int cmdPublish(const PublishOptions& opts) {
    const fs::path root = fs::current_path();
    const fs::path manifestPath = root / kManifestFile;

    if (!fs::exists(manifestPath)) {
        std::cerr << "error: no " << kManifestFile
                  << " in this directory (run 'opg init' first)\n";
        return 1;
    }

    try {
        const Manifest m = Manifest::load(manifestPath);

        const std::string token = resolveToken(opts.token);
        if (!opts.dryRun && token.empty()) {
            std::cerr << "error: no publish token. Set OPG_TOKEN (or pass --token).\n"
                         "       Ask your registry admin for one, or for a local registry run:\n"
                         "       python registry/server.py create-user <name>\n";
            return 1;
        }

        // Build the archive outside the project so it can never pack itself.
        TempFile tmp{fs::temp_directory_path() /
                     ("opg-publish-" + m.name + "-" + m.version + "-" +
                      std::to_string(::getpid()) + ".tar.gz")};

        const auto files = collectPackageFiles(root, tmp.path);
        warnIfEntryPointMissing(m, files);

        std::cout << "opg publish: " << m.name << "@" << m.version << "\n\n";
        for (const auto& e : files) {
            std::cout << "  " << e.rel << "  (" << humanSize(e.size) << ")\n";
        }

        const PackResult r = writePackage(tmp.path, files);
        std::cout << "\n" << r.fileCount << " file(s), " << humanSize(r.archiveSize)
                  << " compressed\n  sha256: " << r.sha256 << "\n";

        if (opts.dryRun) {
            std::cout << "(dry run: nothing uploaded)\n";
            return 0;
        }

        std::string body;
        {
            std::ifstream in(tmp.path, std::ios::binary);
            if (!in) throw std::runtime_error("cannot read " + tmp.path.string());
            body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        RegistryClient client(resolveRegistryUrl(opts.registry), token);
        std::cout << "\nUploading to " << client.baseUrl() << " ...\n";
        client.putArchive("/api/packages/" + m.name + "/" + m.version, body, r.sha256);

        std::cout << "Published " << m.name << "@" << m.version << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

}  // namespace opg
