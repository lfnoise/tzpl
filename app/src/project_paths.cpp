#include "project_paths.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace tzplapp {

std::string findProjectRoot(std::string const& filePath) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(filePath, ec);
    if (ec || p.empty()) return {};
    fs::path dir = p.parent_path();
    while (!dir.empty() && dir != dir.root_path()) {
        if (fs::is_regular_file(dir / "tzpl-config", ec)) {
            return dir.string();
        }
        dir = dir.parent_path();
    }
    return {};
}

std::string projectConfigFile(std::string const& projectRoot) {
    if (projectRoot.empty()) return {};
    std::error_code ec;
    fs::path candidate = fs::path(projectRoot) / "tzpl-config";
    return fs::is_regular_file(candidate, ec) ? candidate.string()
                                              : std::string{};
}

}  // namespace tzplapp
