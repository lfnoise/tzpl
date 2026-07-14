#include "module_paths.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace ts {

std::vector<std::string> envModulePaths() {
    std::vector<std::string> paths;
    if (const char* envPath = std::getenv("TZPL_PATH")) {
        std::string pathStr(envPath);
        size_t start = 0;
        while (start < pathStr.size()) {
            size_t end = pathStr.find(':', start);
            if (end == std::string::npos) end = pathStr.size();
            std::string dir = pathStr.substr(start, end - start);
            if (!dir.empty()) {
                paths.push_back(std::move(dir));
            }
            start = end + 1;
        }
    }
    return paths;
}

// Absolute path of the running executable with symlinks resolved (so a
// symlinked CLI still walks up from its real install location). Empty on
// failure.
static fs::path executablePath() {
    std::error_code ec;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    buf.resize(std::strlen(buf.c_str()));
    fs::path exe = fs::canonical(buf, ec);
#else
    fs::path exe = fs::canonical("/proc/self/exe", ec);
#endif
    return ec ? fs::path{} : exe;
}

std::vector<std::string> defaultModulePaths(
    std::vector<std::string> const& fallbacks)
{
    std::vector<std::string> result;
    std::error_code ec;

    auto addIfDir = [&](fs::path const& dir) {
        if (!fs::is_directory(dir, ec)) return;
        fs::path canon = fs::weakly_canonical(dir, ec);
        if (ec) return;
        std::string s = canon.string();
        for (auto const& existing : result) {
            if (existing == s) return;
        }
        result.push_back(std::move(s));
    };

    // The distribution's examples/ (modules dir's sibling) also joins the
    // search path, after the stdlib proper: examples import each other
    // (instrument_synthdefs etc.) and user scripts import them by name.
    auto addModulesAndExamples = [&](fs::path const& root) {
        addIfDir(root / "modules");
        addIfDir(root / "examples");
    };

    // 1. $TZPL_HOME -- explicit override
    if (const char* home = std::getenv("TZPL_HOME"); home && *home) {
        addModulesAndExamples(fs::path(home));
    }

    // 2. Nearest ancestor of the executable containing modules/. Depth-capped
    // so a stray modules/ far up the tree can't be picked up by accident; six
    // levels covers the deepest expected layout (.app/Contents/MacOS inside a
    // distribution folder).
    if (fs::path exe = executablePath(); !exe.empty()) {
        fs::path dir = exe.parent_path();
        for (int depth = 0;
             depth < 6 && !dir.empty() && dir != dir.root_path();
             ++depth, dir = dir.parent_path()) {
            if (fs::is_directory(dir / "modules", ec)) {
                addModulesAndExamples(dir);
                break;
            }
        }
    }

    // 3. Compiled-in source-tree fallbacks (dev builds)
    for (auto const& f : fallbacks) {
        addIfDir(f);
    }

    return result;
}

}  // namespace ts
