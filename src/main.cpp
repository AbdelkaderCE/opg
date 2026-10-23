#include <iostream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>

#include "commands.hpp"

int main(int argc, char** argv) {
    CLI::App app{"opg - the Oker package manager", "opg"};
    app.set_version_flag("-V,--version", std::string("opg ") + OPG_VERSION);
    app.require_subcommand(1);

    // opg init
    opg::InitOptions initOpts;
    auto* init = app.add_subcommand("init", "Create an oker.json in the current directory");
    init->add_option("--name", initOpts.name, "Package name");
    init->add_option("--pkg-version", initOpts.version, "Initial version (default 0.1.0)");
    init->add_option("--description", initOpts.description, "Short description");
    init->add_option("--author", initOpts.author, "Author");
    init->add_option("--license", initOpts.license, "License identifier (default MIT)");
    init->add_option("--main", initOpts.main, "Entry point file");
    init->add_flag("-y,--yes", initOpts.yes, "Accept defaults without prompting");
    init->add_flag("-f,--force", initOpts.force, "Overwrite an existing oker.json");

    // opg pack
    opg::PackOptions packOpts;
    auto* pack = app.add_subcommand("pack", "Bundle the project into a .tar.gz archive");
    pack->add_option("-o,--output", packOpts.output,
                     "Output file or directory (default ./<name>-<version>.tar.gz)");
    pack->add_flag("-n,--dry-run", packOpts.dryRun, "List the files without writing an archive");

    const std::string registryHelp =
        "Registry URL (default: $OPG_REGISTRY or http://localhost:4873)";

    // opg install
    opg::InstallOptions installOpts;
    auto* install = app.add_subcommand(
        "install", "Install dependencies into oker_modules/ (or add new ones)");
    install->add_option("packages", installOpts.packages,
                        "Packages to add, as name or name@1.2.3 (default: everything in oker.json)");
    install->add_option("--registry", installOpts.registry, registryHelp);
    install->add_flag("--no-save", installOpts.noSave, "Do not modify oker.json or oker.lock");
    install->add_flag("--frozen", installOpts.frozen,
                      "Fail unless oker.lock matches oker.json; never modify either (for CI)");
    install->add_flag("-f,--force", installOpts.force, "Reinstall even if already present");

    // opg publish
    opg::PublishOptions publishOpts;
    auto* publish = app.add_subcommand("publish", "Upload the package to the registry");
    publish->add_option("--registry", publishOpts.registry, registryHelp);
    publish->add_option("--token", publishOpts.token,
                        "Publish token (default: $OPG_TOKEN; prefer the variable, since "
                        "command lines show up in shell history)");
    publish->add_flag("-n,--dry-run", publishOpts.dryRun, "Build and show the package without uploading");

    // opg resolve (debugging aid: where would this import be loaded from?)
    opg::ResolveOptions resolveOpts;
    auto* resolve = app.add_subcommand("resolve", "Show which file an import would load");
    resolve->add_option("spec", resolveOpts.spec, "Import string, e.g. libmath, libmath/util, ./helper")
        ->required();
    resolve->add_option("--from", resolveOpts.from,
                        "File containing the import (default: ./main.oker)");

    CLI11_PARSE(app, argc, argv);

    if (init->parsed()) return opg::cmdInit(initOpts);
    if (pack->parsed()) return opg::cmdPack(packOpts);
    if (install->parsed()) return opg::cmdInstall(installOpts);
    if (publish->parsed()) return opg::cmdPublish(publishOpts);
    if (resolve->parsed()) return opg::cmdResolve(resolveOpts);
    return 0;
}
