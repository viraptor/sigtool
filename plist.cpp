#include "plist.h"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace SigTool {

namespace {

class Parser {
public:
    explicit Parser(const std::string& src) : src_(src), pos_(0) {}

    std::shared_ptr<PlistValue> parseDocument() {
        skipProlog();
        expectOpenTag("plist");
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        expectCloseTag("plist");
        return value;
    }

private:
    const std::string& src_;
    size_t pos_;

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error{"plist parse error at offset "
                                 + std::to_string(pos_) + ": " + msg};
    }

    bool eof() const { return pos_ >= src_.size(); }

    char peek() {
        if (eof()) fail("unexpected EOF");
        return src_[pos_];
    }

    void skipWhitespace() {
        while (!eof() && std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            pos_++;
        }
    }

    bool startsWith(const char* needle) {
        size_t n = 0;
        while (needle[n]) n++;
        if (pos_ + n > src_.size()) return false;
        return src_.compare(pos_, n, needle) == 0;
    }

    void skipProlog() {
        while (!eof()) {
            skipWhitespace();
            if (startsWith("<?")) {
                size_t end = src_.find("?>", pos_);
                if (end == std::string::npos) fail("unterminated <?...?>");
                pos_ = end + 2;
            } else if (startsWith("<!--")) {
                size_t end = src_.find("-->", pos_);
                if (end == std::string::npos) fail("unterminated comment");
                pos_ = end + 3;
            } else if (startsWith("<!DOCTYPE")) {
                size_t end = src_.find('>', pos_);
                if (end == std::string::npos) fail("unterminated DOCTYPE");
                pos_ = end + 1;
            } else {
                return;
            }
        }
    }

    // Consume an opening tag like "<name>" or "<name attr=...>". Self-closing
    // tags must be handled by the caller.
    void expectOpenTag(const std::string& name) {
        skipWhitespace();
        if (peek() != '<') fail("expected '<' for open tag <" + name + ">");
        pos_++;
        for (char c : name) {
            if (eof() || src_[pos_] != c)
                fail("expected open tag <" + name + ">");
            pos_++;
        }
        size_t end = src_.find('>', pos_);
        if (end == std::string::npos) fail("unterminated open tag");
        if (end > 0 && src_[end - 1] == '/')
            fail("expected open tag, got self-closing <" + name + "/>");
        pos_ = end + 1;
    }

    void expectCloseTag(const std::string& name) {
        skipWhitespace();
        std::string expected = "</" + name + ">";
        if (!startsWith(expected.c_str()))
            fail("expected close tag " + expected);
        pos_ += expected.size();
    }

    // Read an element name from the current "<" position. Returns the name and
    // sets selfClosing to true if it ended with "/>". Leaves pos_ just past the
    // tag's closing ">".
    std::string readOpenOrSelfTag(bool& selfClosing) {
        if (peek() != '<') fail("expected '<'");
        pos_++;
        std::string name;
        while (!eof() && src_[pos_] != '>' && src_[pos_] != '/'
               && !std::isspace(static_cast<unsigned char>(src_[pos_]))) {
            name.push_back(src_[pos_++]);
        }
        // skip attributes (we don't use them)
        while (!eof() && src_[pos_] != '>' && src_[pos_] != '/') pos_++;
        selfClosing = false;
        if (!eof() && src_[pos_] == '/') {
            selfClosing = true;
            pos_++;
        }
        if (eof() || src_[pos_] != '>') fail("unterminated tag");
        pos_++;
        return name;
    }

    // Read text content up to the next '<', decoding XML entities.
    std::string readText() {
        std::string out;
        while (!eof() && src_[pos_] != '<') {
            char c = src_[pos_];
            if (c == '&') {
                size_t semi = src_.find(';', pos_);
                if (semi == std::string::npos) fail("unterminated entity");
                std::string ent = src_.substr(pos_ + 1, semi - pos_ - 1);
                pos_ = semi + 1;
                if (ent == "amp") out.push_back('&');
                else if (ent == "lt") out.push_back('<');
                else if (ent == "gt") out.push_back('>');
                else if (ent == "quot") out.push_back('"');
                else if (ent == "apos") out.push_back('\'');
                else if (!ent.empty() && ent[0] == '#') {
                    int base = 10;
                    size_t i = 1;
                    if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                        base = 16;
                        i = 2;
                    }
                    long cp = std::strtol(ent.c_str() + i, nullptr, base);
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
                    } else if (cp < 0x10000) {
                        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
                    } else {
                        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
                    }
                } else {
                    fail("unknown entity &" + ent + ";");
                }
            } else {
                out.push_back(c);
                pos_++;
            }
        }
        return out;
    }

    std::shared_ptr<PlistValue> parseValue() {
        skipWhitespace();
        if (peek() != '<') fail("expected element");
        bool selfClosing = false;
        size_t startPos = pos_;
        std::string name = readOpenOrSelfTag(selfClosing);

        auto val = std::make_shared<PlistValue>();

        if (name == "true") {
            if (!selfClosing) expectCloseTag("true");
            val->type = PlistValue::Type::Bool;
            val->boolValue = true;
        } else if (name == "false") {
            if (!selfClosing) expectCloseTag("false");
            val->type = PlistValue::Type::Bool;
            val->boolValue = false;
        } else if (name == "integer") {
            if (selfClosing) fail("<integer/> with no value");
            std::string text = readText();
            expectCloseTag("integer");
            val->type = PlistValue::Type::Integer;
            // Trim whitespace.
            size_t b = 0, e = text.size();
            while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) b++;
            while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) e--;
            val->intValue = std::strtoll(text.c_str() + b, nullptr, 10);
        } else if (name == "string") {
            val->type = PlistValue::Type::String;
            if (!selfClosing) {
                val->stringValue = readText();
                expectCloseTag("string");
            }
        } else if (name == "array") {
            val->type = PlistValue::Type::Array;
            if (!selfClosing) {
                while (true) {
                    skipWhitespace();
                    if (startsWith("</array>")) break;
                    val->arrayValue.push_back(parseValue());
                }
                expectCloseTag("array");
            }
        } else if (name == "dict") {
            val->type = PlistValue::Type::Dict;
            if (!selfClosing) {
                while (true) {
                    skipWhitespace();
                    if (startsWith("</dict>")) break;
                    expectOpenTag("key");
                    std::string key = readText();
                    expectCloseTag("key");
                    auto v = parseValue();
                    val->dictValue.emplace_back(std::move(key), std::move(v));
                }
                expectCloseTag("dict");
            }
        } else {
            (void)startPos;
            fail("unsupported plist element <" + name + ">");
        }
        return val;
    }
};

} // namespace

std::shared_ptr<PlistValue> parsePlist(const std::string& xml) {
    Parser p{xml};
    return p.parseDocument();
}

namespace {

// Minimal binary plist reader. Supports bool, int, string (ASCII + UTF-16BE),
// array, and dict — everything we need to read Info.plist values. Throws on
// data/real/date/UID/set since those don't appear in the keys we care about.
class BinaryPlistReader {
public:
    explicit BinaryPlistReader(const std::string& bytes) : src_(bytes) {
        if (src_.size() < 40 || src_.compare(0, 8, "bplist00") != 0) {
            throw std::runtime_error{"not a bplist00 binary plist"};
        }
        const uint8_t* trailer = reinterpret_cast<const uint8_t*>(src_.data())
                                 + src_.size() - 32;
        offsetIntSize_ = trailer[6];
        objectRefSize_ = trailer[7];
        numObjects_ = readBE64(trailer + 8);
        topObject_ = readBE64(trailer + 16);
        offsetTableOffset_ = readBE64(trailer + 24);
        if (offsetIntSize_ == 0 || objectRefSize_ == 0
            || offsetTableOffset_ + numObjects_ * offsetIntSize_ > src_.size() - 32) {
            throw std::runtime_error{"binary plist trailer out of range"};
        }
    }

    std::shared_ptr<PlistValue> readTop() {
        return readObject(topObject_);
    }

private:
    const std::string& src_;
    uint8_t offsetIntSize_;
    uint8_t objectRefSize_;
    uint64_t numObjects_;
    uint64_t topObject_;
    uint64_t offsetTableOffset_;

    static uint64_t readBE(const uint8_t* p, size_t n) {
        uint64_t v = 0;
        for (size_t i = 0; i < n; i++) v = (v << 8) | p[i];
        return v;
    }

    static uint64_t readBE64(const uint8_t* p) { return readBE(p, 8); }

    uint64_t objectOffset(uint64_t idx) {
        if (idx >= numObjects_) {
            throw std::runtime_error{"bplist object index out of range"};
        }
        const uint8_t* table = reinterpret_cast<const uint8_t*>(src_.data())
                               + offsetTableOffset_;
        return readBE(table + idx * offsetIntSize_, offsetIntSize_);
    }

    // Parse the size for a marker; if low nibble is 0xF, the next object is an
    // integer encoding the actual size. Updates `pos` to point past the size.
    uint64_t readSize(uint8_t marker, uint64_t& pos) {
        uint8_t low = marker & 0x0f;
        if (low != 0x0f) return low;
        if (pos >= src_.size()) throw std::runtime_error{"bplist truncated"};
        uint8_t intMarker = static_cast<uint8_t>(src_[pos++]);
        if ((intMarker & 0xf0) != 0x10) {
            throw std::runtime_error{"bplist extended size not int"};
        }
        size_t nbytes = size_t{1} << (intMarker & 0x0f);
        if (pos + nbytes > src_.size()) throw std::runtime_error{"bplist truncated"};
        uint64_t v = readBE(reinterpret_cast<const uint8_t*>(src_.data()) + pos, nbytes);
        pos += nbytes;
        return v;
    }

    uint64_t readRef(uint64_t& pos) {
        if (pos + objectRefSize_ > src_.size()) {
            throw std::runtime_error{"bplist truncated reading ref"};
        }
        uint64_t v = readBE(reinterpret_cast<const uint8_t*>(src_.data()) + pos,
                            objectRefSize_);
        pos += objectRefSize_;
        return v;
    }

    std::shared_ptr<PlistValue> readObject(uint64_t idx) {
        uint64_t pos = objectOffset(idx);
        if (pos >= src_.size()) throw std::runtime_error{"bplist object out of range"};
        uint8_t marker = static_cast<uint8_t>(src_[pos++]);
        auto val = std::make_shared<PlistValue>();
        switch (marker & 0xf0) {
            case 0x00: {
                if (marker == 0x08) { val->type = PlistValue::Type::Bool; val->boolValue = false; return val; }
                if (marker == 0x09) { val->type = PlistValue::Type::Bool; val->boolValue = true; return val; }
                if (marker == 0x00) { val->type = PlistValue::Type::String; return val; } // null → empty string
                throw std::runtime_error{"unsupported bplist marker"};
            }
            case 0x10: {
                size_t nbytes = size_t{1} << (marker & 0x0f);
                if (pos + nbytes > src_.size()) throw std::runtime_error{"bplist truncated int"};
                val->type = PlistValue::Type::Integer;
                if (nbytes <= 8) {
                    uint64_t u = readBE(reinterpret_cast<const uint8_t*>(src_.data()) + pos, nbytes);
                    if (nbytes == 8) val->intValue = static_cast<int64_t>(u);
                    else val->intValue = static_cast<int64_t>(u);
                } else {
                    val->intValue = 0; // 16-byte ints rare; we don't need them
                }
                return val;
            }
            case 0x50: { // ASCII string
                uint64_t n = readSize(marker, pos);
                if (pos + n > src_.size()) throw std::runtime_error{"bplist truncated string"};
                val->type = PlistValue::Type::String;
                val->stringValue.assign(src_, pos, n);
                return val;
            }
            case 0x60: { // UTF-16BE string
                uint64_t n = readSize(marker, pos);
                if (pos + n * 2 > src_.size()) throw std::runtime_error{"bplist truncated utf16"};
                val->type = PlistValue::Type::String;
                const uint8_t* p = reinterpret_cast<const uint8_t*>(src_.data()) + pos;
                for (uint64_t i = 0; i < n; i++) {
                    uint32_t cp = (uint32_t(p[i*2]) << 8) | p[i*2+1];
                    if (cp < 0x80) {
                        val->stringValue.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        val->stringValue.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                        val->stringValue.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
                    } else {
                        val->stringValue.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                        val->stringValue.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                        val->stringValue.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
                    }
                }
                return val;
            }
            case 0xa0: { // array
                uint64_t n = readSize(marker, pos);
                val->type = PlistValue::Type::Array;
                std::vector<uint64_t> refs(n);
                for (uint64_t i = 0; i < n; i++) refs[i] = readRef(pos);
                for (auto r : refs) val->arrayValue.push_back(readObject(r));
                return val;
            }
            case 0xd0: { // dict
                uint64_t n = readSize(marker, pos);
                val->type = PlistValue::Type::Dict;
                std::vector<uint64_t> keyRefs(n), valRefs(n);
                for (uint64_t i = 0; i < n; i++) keyRefs[i] = readRef(pos);
                for (uint64_t i = 0; i < n; i++) valRefs[i] = readRef(pos);
                for (uint64_t i = 0; i < n; i++) {
                    auto k = readObject(keyRefs[i]);
                    if (k->type != PlistValue::Type::String) {
                        throw std::runtime_error{"bplist dict key not a string"};
                    }
                    val->dictValue.emplace_back(k->stringValue, readObject(valRefs[i]));
                }
                return val;
            }
            default:
                throw std::runtime_error{"unsupported bplist marker (data/real/date/uid not handled)"};
        }
    }
};

} // namespace

std::shared_ptr<PlistValue> parsePlistAuto(const std::string& bytes) {
    if (bytes.size() >= 8 && bytes.compare(0, 8, "bplist00") == 0) {
        BinaryPlistReader r{bytes};
        return r.readTop();
    }
    return parsePlist(bytes);
}

};
