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
        if (fs::is_regular_file(dir / "config", ec)) {
            return dir.string();
        }
        dir = dir.parent_path();
    }
    return {};
}

}  // namespace tzplapp
