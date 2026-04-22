#include "commands.h"
#include <CLI11.hpp>
#include <cstring>
#include <iostream>

static int run(int argc, char **argv);

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "codesign: error: " << e.what() << std::endl;
        return 1;
    }
}

static int run(int argc, char **argv) {
    // Apple's --timestamp uses an optional joined value:
    //   --timestamp        request default TSA (not supported here)
    //   --timestamp=none   suppress timestamping (no-op for ad-hoc)
    //   --timestamp=URL    request a specific TSA (not supported here)
    // CLI11 can't express "joined-only optional value", so handle it before
    // parsing and strip it from argv.
    std::vector<char*> filtered;
    filtered.reserve(argc);
    bool sawTimestamp = false;
    bool timestampHasValue = false;
    std::string timestampValue;
    for (int i = 0; i < argc; i++) {
        if (std::strcmp(argv[i], "--timestamp") == 0) {
            sawTimestamp = true;
            continue;
        }
        if (std::strncmp(argv[i], "--timestamp=", 12) == 0) {
            sawTimestamp = true;
            timestampHasValue = true;
            timestampValue = argv[i] + 12;
            continue;
        }
        filtered.push_back(argv[i]);
    }
    if (sawTimestamp && (!timestampHasValue || timestampValue != "none")) {
        throw std::runtime_error{
                std::string{"--timestamp only supports '=none' (TSA timestamping "
                            "is not available with ad-hoc signatures)"}};
    }
    int filteredArgc = static_cast<int>(filtered.size());
    char** filteredArgv = filtered.data();

    CLI::App app{"codesign"};

    std::string identity, identifier, entitlements, optionsFlags, preserveMetadata;
    bool force = false;
    bool generateEntitlementDer = false;
    std::vector<std::string> files;
    app.add_option("-s,--sign", identity, "Code signing identity")->required();
    app.add_option("-i,--identifier", identifier, "File identifier");
    app.add_flag("-f,--force", force, "Replace any existing signatures");
    app.add_option("--entitlements", entitlements, "Entitlements plist");
    app.add_flag("--generate-entitlement-der", generateEntitlementDer,
                 "Embed DER-encoded entitlements alongside the XML blob");
    app.add_option("-o,--options", optionsFlags,
                   "Comma-separated signing options; only 'runtime' is supported");
    app.add_option("--preserve-metadata", preserveMetadata,
                   "Reuse fields from existing signature; comma-separated list of "
                   "'identifier', 'entitlements', 'flags'");
    app.add_option("files", files, "Files to sign");

    CLI11_PARSE(app, filteredArgc, filteredArgv);

    if (identity != std::string{"-"}) {
        throw std::runtime_error{
                std::string{"Only ad-hoc identities supported, requested: '"} + identity + "'"};
    }

    bool hardenedRuntime = false;
    if (!optionsFlags.empty()) {
        size_t start = 0;
        while (start <= optionsFlags.size()) {
            size_t comma = optionsFlags.find(',', start);
            if (comma == std::string::npos) comma = optionsFlags.size();
            std::string opt = optionsFlags.substr(start, comma - start);
            if (opt == "runtime") {
                hardenedRuntime = true;
            } else if (!opt.empty()) {
                throw std::runtime_error{
                        "-o option '" + opt + "' is not supported "
                        "(only 'runtime' is recognised)"};
            }
            if (comma == optionsFlags.size()) break;
            start = comma + 1;
        }
    }

    bool preserveIdentifier = false;
    bool preserveEntitlements = false;
    bool preserveFlags = false;
    if (!preserveMetadata.empty()) {
        size_t start = 0;
        while (start <= preserveMetadata.size()) {
            size_t comma = preserveMetadata.find(',', start);
            if (comma == std::string::npos) comma = preserveMetadata.size();
            std::string key = preserveMetadata.substr(start, comma - start);
            if (key == "identifier") preserveIdentifier = true;
            else if (key == "entitlements") preserveEntitlements = true;
            else if (key == "flags") preserveFlags = true;
            else if (!key.empty()) {
                throw std::runtime_error{
                        "--preserve-metadata key '" + key + "' is not supported "
                        "(supported: identifier, entitlements, flags)"};
            }
            if (comma == preserveMetadata.size()) break;
            start = comma + 1;
        }
    }

    SigTool::Commands::CodesignOptions options{
            .identifier = identifier,
            .entitlements = entitlements,
            .force = force,
            .generateEntitlementDer = generateEntitlementDer,
            .hardenedRuntime = hardenedRuntime,
            .preserveIdentifier = preserveIdentifier,
            .preserveEntitlements = preserveEntitlements,
            .preserveFlags = preserveFlags,
    };

    for (const auto &f : files) {
	SigTool::Commands::codesign(options, f);
    }

    return 0;
}
