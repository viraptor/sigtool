#include "bundle.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

#include "plist.h"

namespace SigTool {

namespace {

bool isDir(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string trimSlash(std::string p) {
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string basename(const std::string& s) {
    auto slash = s.find_last_of('/');
    return slash == std::string::npos ? s : s.substr(slash + 1);
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size()
           && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string slurpFile(const std::string& path) {
    std::ifstream in(path, std::ifstream::binary);
    if (!in.is_open()) {
        throw std::runtime_error{"opening '" + path + "': read failed"};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Look up a top-level string value in a parsed plist.
std::string getString(const PlistValue& dict, const std::string& key) {
    if (dict.type != PlistValue::Type::Dict) return {};
    for (const auto& kv : dict.dictValue) {
        if (kv.first == key && kv.second->type == PlistValue::Type::String) {
            return kv.second->stringValue;
        }
    }
    return {};
}

// Resolve a possibly-symlinked Versions/Current to a concrete version dir name.
std::string resolveCurrentVersion(const std::string& versionsDir) {
    std::string current = versionsDir + "/Current";
    char buf[1024];
    ssize_t n = ::readlink(current.c_str(), buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        return std::string{buf};
    }
    // Fallback: pick "A".
    if (isDir(versionsDir + "/A")) return "A";
    throw std::runtime_error{"cannot resolve framework Versions/Current under " + versionsDir};
}

void populateFromInfoPlist(Bundle& b) {
    if (b.infoPlistPath.empty() || !isFile(b.infoPlistPath)) return;
    auto plist = parsePlistAuto(slurpFile(b.infoPlistPath));
    b.identifier = getString(*plist, "CFBundleIdentifier");
}

} // namespace

Bundle detectBundle(const std::string& rawPath) {
    Bundle b{};
    std::string path = trimSlash(rawPath);
    b.root = path;

    if (isFile(path)) {
        b.type = Bundle::Type::Single;
        b.binaryPath = path;
        return b;
    }

    if (!isDir(path)) {
        throw std::runtime_error{"path does not exist: '" + rawPath + "'"};
    }

    // Framework: Foo.framework or Foo.framework/Versions/X
    {
        std::string fwRoot;
        std::string version;
        if (endsWith(path, ".framework")) {
            fwRoot = path;
            version = resolveCurrentVersion(fwRoot + "/Versions");
        } else {
            // Maybe Foo.framework/Versions/X
            auto slash = path.find_last_of('/');
            if (slash != std::string::npos) {
                std::string parent = path.substr(0, slash);
                if (endsWith(parent, "/Versions")) {
                    std::string grand = parent.substr(0, parent.size() - 9);
                    if (endsWith(grand, ".framework")) {
                        fwRoot = grand;
                        version = path.substr(slash + 1);
                    }
                }
            }
        }
        if (!fwRoot.empty()) {
            std::string fwName = basename(fwRoot);
            fwName = fwName.substr(0, fwName.size() - sizeof(".framework") + 1);
            b.type = Bundle::Type::Framework;
            b.root = fwRoot;
            b.contentsRoot = fwRoot + "/Versions/" + version;
            b.binaryPath = b.contentsRoot + "/" + fwName;
            std::string ip = b.contentsRoot + "/Resources/Info.plist";
            if (isFile(ip)) b.infoPlistPath = ip;
            populateFromInfoPlist(b);
            if (!isFile(b.binaryPath)) {
                throw std::runtime_error{
                        "framework binary not found: '" + b.binaryPath + "'"};
            }
            return b;
        }
    }

    // App / generic .app-style bundle: Foo.app
    if (endsWith(path, ".app") || endsWith(path, ".bundle") || endsWith(path, ".xpc")) {
        b.type = Bundle::Type::App;
        b.root = path;
        b.contentsRoot = path + "/Contents";
        b.infoPlistPath = b.contentsRoot + "/Info.plist";
        if (!isFile(b.infoPlistPath)) {
            throw std::runtime_error{
                    "bundle Info.plist not found at '" + b.infoPlistPath + "'"};
        }
        auto plist = parsePlistAuto(slurpFile(b.infoPlistPath));
        b.identifier = getString(*plist, "CFBundleIdentifier");
        std::string execName = getString(*plist, "CFBundleExecutable");
        if (execName.empty()) {
            std::string base = basename(path);
            for (const auto& suffix : {".app", ".bundle", ".xpc"}) {
                if (endsWith(base, suffix)) {
                    execName = base.substr(0, base.size() - std::string{suffix}.size());
                    break;
                }
            }
        }
        b.binaryPath = b.contentsRoot + "/MacOS/" + execName;
        if (!isFile(b.binaryPath)) {
            throw std::runtime_error{
                    "bundle executable not found: '" + b.binaryPath + "'"};
        }
        return b;
    }

    throw std::runtime_error{
            "directory '" + rawPath + "' is not a recognised bundle "
            "(*.framework, *.app, *.bundle, *.xpc)"};
}

};
