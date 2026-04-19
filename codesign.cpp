#include "commands.h"
#include <CLI11.hpp>
#include <cstring>

int main(int argc, char **argv) {
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

    std::string identity, identifier, entitlements;
    bool force = false;
    bool generateEntitlementDER = false;
    std::vector<std::string> files;
    app.add_option("-s,--sign", identity, "Code signing identity")->required();
    app.add_option("-i,--identifier", identifier, "File identifier");
    app.add_flag("-f,--force", force, "Replace any existing signatures");
    app.add_option("--entitlements", entitlements, "Entitlements plist");
    app.add_flag("--generate-entitlement-der", generateEntitlementDER,
                 "Also embed DER-encoded entitlements");
    app.add_option("files", files, "Files to sign");

    CLI11_PARSE(app, filteredArgc, filteredArgv);

    if (identity != std::string{"-"}) {
        throw std::runtime_error{
                std::string{"Only ad-hoc identities supported, requested: '"} + identity + "'"};
    }

    SigTool::Commands::CodesignOptions options{
            .identifier = identifier,
            .entitlements = entitlements,
            .force = force,
            .generateEntitlementDER = generateEntitlementDER,
    };

    for (const auto &f : files) {
	SigTool::Commands::codesign(options, f);
    }

    return 0;
}
