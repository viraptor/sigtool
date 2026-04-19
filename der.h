#ifndef SIGTOOL_DER_H
#define SIGTOOL_DER_H

#include <cstdint>
#include <string>
#include <vector>

#include "plist.h"

namespace SigTool {

// Encode a top-level entitlements plist value as Apple's entitlements DER blob
// payload (the bytes that follow the 8-byte CSMAGIC_EMBEDDED_DER_ENTITLEMENTS
// header). The top value must be a Dict.
std::vector<uint8_t> encodeEntitlementsDER(const PlistValue& root);

};

#endif // SIGTOOL_DER_H
