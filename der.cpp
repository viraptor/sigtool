#include "der.h"

#include <algorithm>
#include <stdexcept>

namespace SigTool {

namespace {

// DER tags used for Apple's entitlements schema.
constexpr uint8_t TAG_BOOLEAN     = 0x01;
constexpr uint8_t TAG_INTEGER     = 0x02;
constexpr uint8_t TAG_UTF8STRING  = 0x0c;
constexpr uint8_t TAG_SEQUENCE    = 0x30; // CONSTRUCTED, universal 16
constexpr uint8_t TAG_DICT        = 0xb0; // CONSTRUCTED, context-specific 16
constexpr uint8_t TAG_TOP         = 0x70; // CONSTRUCTED, application 16

void appendLength(std::vector<uint8_t>& out, size_t len) {
    if (len < 0x80) {
        out.push_back(static_cast<uint8_t>(len));
        return;
    }
    uint8_t buf[8];
    int n = 0;
    while (len > 0) {
        buf[n++] = static_cast<uint8_t>(len & 0xff);
        len >>= 8;
    }
    out.push_back(static_cast<uint8_t>(0x80 | n));
    for (int i = n - 1; i >= 0; i--) out.push_back(buf[i]);
}

void appendTLV(std::vector<uint8_t>& out, uint8_t tag,
               const std::vector<uint8_t>& body) {
    out.push_back(tag);
    appendLength(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
}

std::vector<uint8_t> encodeInteger(int64_t v) {
    // Two's-complement signed, minimal byte count.
    uint8_t bytes[9];
    int n = 0;
    uint64_t u = static_cast<uint64_t>(v);
    // Always emit at least one byte.
    bytes[n++] = static_cast<uint8_t>(u & 0xff);
    u >>= 8;
    if (v >= 0) {
        while (u != 0) {
            bytes[n++] = static_cast<uint8_t>(u & 0xff);
            u >>= 8;
        }
        // If top bit of high byte is set, prepend 0x00 to keep positive.
        if (bytes[n - 1] & 0x80) bytes[n++] = 0x00;
    } else {
        // For negative, keep adding bytes until the top byte's high bit is 1
        // and the remaining bits aren't all 1s in a way that could be trimmed.
        int64_t s = v >> 8;
        while (!(s == -1 && (bytes[n - 1] & 0x80))) {
            bytes[n++] = static_cast<uint8_t>(s & 0xff);
            s >>= 8;
        }
    }
    std::vector<uint8_t> out(n);
    for (int i = 0; i < n; i++) out[i] = bytes[n - 1 - i];
    return out;
}

void encodeValue(const PlistValue& v, std::vector<uint8_t>& out);

void encodeDictBody(const PlistValue& dict, std::vector<uint8_t>& out) {
    // Sort entries by key (lexicographic byte order, matching Apple).
    std::vector<std::pair<std::string, std::shared_ptr<PlistValue>>> entries =
            dict.dictValue;
    std::sort(entries.begin(), entries.end(),
              [](const std::pair<std::string, std::shared_ptr<PlistValue>>& a,
                 const std::pair<std::string, std::shared_ptr<PlistValue>>& b) {
                  return a.first < b.first;
              });

    for (const auto& kv : entries) {
        std::vector<uint8_t> entry;
        std::vector<uint8_t> keyBody(kv.first.begin(), kv.first.end());
        appendTLV(entry, TAG_UTF8STRING, keyBody);
        encodeValue(*kv.second, entry);
        appendTLV(out, TAG_SEQUENCE, entry);
    }
}

void encodeValue(const PlistValue& v, std::vector<uint8_t>& out) {
    switch (v.type) {
        case PlistValue::Type::Bool: {
            out.push_back(TAG_BOOLEAN);
            out.push_back(0x01);
            out.push_back(v.boolValue ? 0xff : 0x00);
            break;
        }
        case PlistValue::Type::Integer: {
            appendTLV(out, TAG_INTEGER, encodeInteger(v.intValue));
            break;
        }
        case PlistValue::Type::String: {
            std::vector<uint8_t> body(v.stringValue.begin(), v.stringValue.end());
            appendTLV(out, TAG_UTF8STRING, body);
            break;
        }
        case PlistValue::Type::Array: {
            std::vector<uint8_t> body;
            for (const auto& item : v.arrayValue) {
                encodeValue(*item, body);
            }
            appendTLV(out, TAG_SEQUENCE, body);
            break;
        }
        case PlistValue::Type::Dict: {
            std::vector<uint8_t> body;
            encodeDictBody(v, body);
            appendTLV(out, TAG_DICT, body);
            break;
        }
    }
}

} // namespace

std::vector<uint8_t> encodeEntitlementsDER(const PlistValue& root) {
    if (root.type != PlistValue::Type::Dict) {
        throw std::runtime_error{"entitlements root must be a <dict>"};
    }

    // Inner: INTEGER 1 (version) + dict body wrapped in [CONTEXT 16].
    std::vector<uint8_t> inner;
    inner.push_back(TAG_INTEGER);
    inner.push_back(0x01);
    inner.push_back(0x01);

    std::vector<uint8_t> dictBody;
    encodeDictBody(root, dictBody);
    appendTLV(inner, TAG_DICT, dictBody);

    std::vector<uint8_t> out;
    appendTLV(out, TAG_TOP, inner);
    return out;
}

};
