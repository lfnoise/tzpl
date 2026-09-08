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

std::vector<std::string> rootQualifiers(std::vector<std::string> const& paths,
                                        std::string const& home)
{
    // Parent directory components of each path, leaf excluded, root-first.
    // A trailing slash or "." contributes nothing.
    struct Split { std::string leaf; std::vector<std::string> parents; };
    std::vector<Split> split;
    for (auto const& p : paths) {
        fs::path fp = fs::path(p).lexically_normal();
        if (!fp.has_filename()) fp = fp.parent_path();   // "a/b/" -> "a/b"
        Split sp;
        sp.leaf = fp.filename().string();
        for (auto const& part : fp.parent_path()) {
            std::string s = part.string();
            if (!s.empty() && s != "/") sp.parents.push_back(s);
        }
        split.push_back(std::move(sp));
    }

    // The last `depth` parents joined with '/', or the whole parent path
    // when depth covers it (marked so the caller-visible form differs).
    auto suffix = [&](Split const& sp, size_t depth, bool& whole) {
        whole = depth >= sp.parents.size();
        size_t from = whole ? 0 : sp.parents.size() - depth;
        std::string out;
        for (size_t i = from; i < sp.parents.size(); ++i) {
            if (!out.empty()) out += '/';
            out += sp.parents[i];
        }
        return out;
    };

    std::vector<std::string> result(paths.size());
    for (size_t i = 0; i < split.size(); ++i) {
        if (!result[i].empty()) continue;               // already handled
        std::vector<size_t> group;
        for (size_t j = 0; j < split.size(); ++j)
            if (split[j].leaf == split[i].leaf) group.push_back(j);
        if (group.size() < 2) continue;

        size_t maxDepth = 0;
        for (size_t j : group) maxDepth = std::max(maxDepth, split[j].parents.size());
        // Deepest run needed for every pair in the group to differ.
        size_t depth = 1;
        for (; depth < maxDepth; ++depth) {
            bool distinct = true;
            for (size_t a = 0; a < group.size() && distinct; ++a)
                for (size_t b = a + 1; b < group.size(); ++b) {
                    bool wa, wb;
                    if (suffix(split[group[a]], depth, wa)
                        == suffix(split[group[b]], depth, wb)) {
                        distinct = false;
                        break;
                    }
                }
            if (distinct) break;
        }
        for (size_t j : group) {
            bool whole = false;
            std::string s = suffix(split[j], depth, whole);
            if (!whole) {
                result[j] = "\u2026/" + s;
            } else {
                std::string full = "/" + s;
                if (!home.empty() && full.starts_with(home)
                    && (full.size() == home.size() || full[home.size()] == '/'))
                    full = "~" + full.substr(home.size());
                result[j] = full;
            }
        }
    }
    return result;
}

}  // namespace tzplapp
