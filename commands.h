#ifndef SIGTOOL_COMMANDS_H
#define SIGTOOL_COMMANDS_H

#include <string>

namespace SigTool {
namespace Commands {
    struct SignOptions {
        std::string filename;
        std::string identifier;
        std::string entitlements;
        // Raw entitlements XML; takes precedence over `entitlements` (path)
        // when non-empty. Used for --preserve-metadata=entitlements.
        std::string entitlementsData;
        bool generateEntitlementDer;
        // For bundle signing: paths whose hashes seed CodeDirectory special
        // slots 1 (Info.plist) and 3 (CodeResources). Empty = unused.
        std::string infoPlistPath;
        std::string codeResourcesPath;
        bool hardenedRuntime;
        // Runtime version emitted at offset 0x58 when hardenedRuntime is set.
        uint32_t runtimeVersion;
    };

    struct CodesignOptions {
        std::string identifier;
        std::string entitlements;
        bool force;
        bool generateEntitlementDer;
        bool hardenedRuntime;
        // --preserve-metadata categories. When set, fields not explicitly
        // overridden on the CLI are taken from the existing signature.
        bool preserveIdentifier;
        bool preserveEntitlements;
        bool preserveFlags;
    };

    int checkRequiresSignature(const std::string &file);
    int showArch(const std::string &file);
    int showSize(const SignOptions& options);
    int inject(const SignOptions& options);
    int generate(const SignOptions& options);
    int codesign(const CodesignOptions& options, const std::string& file);
};
};

#endif // SIGTOOL_COMMANDS_H
