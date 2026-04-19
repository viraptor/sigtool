#ifndef SIGTOOL_PLIST_H
#define SIGTOOL_PLIST_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace SigTool {

// Minimal XML plist value. Supports the subset Apple's codesign accepts in
// entitlements: bool, integer, string, array, dict.
struct PlistValue {
    enum class Type { Bool, Integer, String, Array, Dict };

    Type type;
    bool boolValue = false;
    int64_t intValue = 0;
    std::string stringValue;
    std::vector<std::shared_ptr<PlistValue>> arrayValue;
    std::vector<std::pair<std::string, std::shared_ptr<PlistValue>>> dictValue;
};

// Parse an XML plist document. Throws std::runtime_error on malformed input or
// unsupported types.
std::shared_ptr<PlistValue> parsePlist(const std::string& xml);

// Parse a plist that may be either XML (`<?xml...`) or binary (`bplist00`).
// Throws std::runtime_error on malformed input or unsupported types.
std::shared_ptr<PlistValue> parsePlistAuto(const std::string& bytes);

};

#endif // SIGTOOL_PLIST_H
