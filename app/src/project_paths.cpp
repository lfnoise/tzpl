#include "project_paths.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace tzplapp {

std::string findProjectRoot(std::string const& filePath) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(filePath, ec);
    if (ec || p.empty()) return {};
    std::string legacyRoot;
    fs::path dir = p.parent_path();
    while (!dir.empty() && dir != dir.root_path()) {
        if (fs::is_regular_file(dir / "tzpl-config", ec)) {
            return dir.string();
        }
        if (legacyRoot.empty() && fs::is_regular_file(dir / "config", ec)) {
            legacyRoot = dir.string();
        }
        dir = dir.parent_path();
    }
    return legacyRoot;
}

std::string projectConfigFile(std::string const& projectRoot) {
    if (projectRoot.empty()) return {};
    std::error_code ec;
    fs::path root(projectRoot);
    if (fs::is_regular_file(root / "tzpl-config", ec))
        return (root / "tzpl-config").string();
    if (fs::is_regular_file(root / "config", ec))
        return (root / "config").string();
    return {};
}

}  // namespace tzplapp
