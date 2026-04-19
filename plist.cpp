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

};
