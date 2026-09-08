// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  definition_finder.cpp
//  app
//

#include "definition_finder.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace tzplapp {

namespace fs = std::filesystem;

std::wstring utf8ToWide(std::string const& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { cp = 0xFFFD; extra = 0; }
        ++i;
        for (int k = 0; k < extra; ++k) {
            if (i >= n || ((unsigned char)s[i] & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
            ++i;
        }
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out += (wchar_t)(0xD800 + (cp >> 10));
                out += (wchar_t)(0xDC00 + (cp & 0x3FF));
                continue;
            }
        }
        out += (wchar_t)cp;
    }
    return out;
}

std::string wideToUtf8(std::wstring const& w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = (uint32_t)w[i];
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size()) {
                uint32_t lo = (uint32_t)w[i + 1];
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                }
            }
        }
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

bool readTextFileWide(std::string const& path, std::wstring& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = utf8ToWide(ss.str());
    return true;
}

namespace {

std::wstring dotted(std::vector<std::wstring> const& path) {
    std::wstring s;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i) s += L'.';
        s += path[i];
    }
    return s;
}

// The name a qualified import is used under: the alias, else the last
// path component.
std::wstring importQualifier(ImportSpec const& spec) {
    if (!spec.alias.empty()) return spec.alias;
    return spec.path.empty() ? std::wstring() : spec.path.back();
}

class Finder {
public:
    explicit Finder(DefinitionQuery const& q) : q_(q) {}

    DefinitionResult run() {
        int start = -1;
        result_.name = identifierAt(q_.text, q_.caret, &start);
        if (result_.name.empty()) return std::move(result_);
        // `q.name`: the caret is on the member of a qualified access.
        if (start >= 2 && q_.text[(size_t)start - 1] == L'.')
            result_.qualifier = identifierAt(q_.text, start - 1);

        ScanResult scan = scanSource(q_.text);
        if (result_.qualifier.empty()) {
            // Innermost enclosing scopes first, then the file's top level.
            std::vector<Definition const*> local, top;
            for (auto const& d : scan.defs) {
                if (d.name != result_.name || !definitionVisibleAt(d, q_.caret))
                    continue;
                (d.topLevel ? top : local).push_back(&d);
            }
            std::stable_sort(local.begin(), local.end(),
                             [](Definition const* a, Definition const* b) {
                                 return a->scopeStart > b->scopeStart;
                             });
            for (auto* d : local) addHit("", L"", *d, q_.text);
            for (auto* d : top) addHit("", L"", *d, q_.text);
        }

        std::string importingDir;
        if (!q_.path.empty())
            importingDir = fs::path(q_.path).parent_path().string();
        for (auto const& spec : scan.imports)
            visitSpec(spec, importingDir, result_.name, result_.qualifier, 0);
        return std::move(result_);
    }

private:
    struct Module {
        std::string path;
        std::wstring text;
        ScanResult scan;
    };

    void addHit(std::string const& path, std::wstring const& modulePath,
                Definition const& d, std::wstring const& text) {
        DefinitionHit h;
        h.path = path;
        h.modulePath = modulePath;
        h.def = d;
        h.lineText = lineTextAt(text, d.start);
        result_.hits.push_back(std::move(h));
    }

    std::string resolve(std::vector<std::wstring> const& path,
                        std::string const& importingDir) {
        std::string rel;
        for (size_t i = 0; i < path.size(); ++i) {
            if (i) rel += '/';
            rel += wideToUtf8(path[i]);
        }
        rel += ".x";
        std::vector<std::string> dirs;
        if (!importingDir.empty()) dirs.push_back(importingDir);
        dirs.insert(dirs.end(), q_.searchDirs.begin(), q_.searchDirs.end());
        std::error_code ec;
        for (auto const& dir : dirs) {
            fs::path candidate = fs::path(dir) / rel;
            if (fs::exists(candidate, ec) && !ec) {
                fs::path canon = fs::weakly_canonical(candidate, ec);
                return (ec ? candidate : canon).string();
            }
        }
        return {};
    }

    Module const* load(std::string const& path) {
        auto it = cache_.find(path);
        if (it != cache_.end()) return it->second ? it->second.get() : nullptr;
        auto m = std::make_unique<Module>();
        m->path = path;
        bool ok = q_.readFile ? q_.readFile(path, m->text)
                              : readTextFileWide(path, m->text);
        if (!ok) { cache_[path] = nullptr; return nullptr; }
        m->scan = scanSource(m->text);
        auto* raw = m.get();
        cache_[path] = std::move(m);
        return raw;
    }

    // One import/export statement of the file in `importingDir`, looking
    // for `name` (unqualified) or `qual.name`.
    void visitSpec(ImportSpec const& spec, std::string const& importingDir,
                   std::wstring const& name, std::wstring const& qual, int depth) {
        if (depth > 8) return;
        std::string path = resolve(spec.path, importingDir);
        if (path.empty()) {
            std::wstring dn = dotted(spec.path);
            if (std::find(result_.unresolvedModules.begin(),
                          result_.unresolvedModules.end(), dn)
                == result_.unresolvedModules.end())
                result_.unresolvedModules.push_back(dn);
            return;
        }
        std::wstring modName = dotted(spec.path);
        bool qualifiedImport = !spec.wildcard && spec.names.empty();
        if (!qual.empty()) {
            if (qualifiedImport) {
                if (importQualifier(spec) == qual)
                    collectExported(path, modName, name, depth);
            } else if (spec.wildcard) {
                // The qualifier may be a module this one re-exports by name.
                if (auto* m = load(path))
                    for (auto const& e : m->scan.imports)
                        if (e.isExport)
                            visitSpec(e, fs::path(path).parent_path().string(),
                                      name, qual, depth + 1);
            }
            return;
        }
        if (spec.wildcard) {
            collectExported(path, modName, name, depth);
        } else if (!spec.names.empty()) {
            for (auto const& [n, alias] : spec.names)
                if ((alias.empty() ? n : alias) == name)
                    collectExported(path, modName, n, depth);
        }
        // A plain qualified import brings no unqualified names.
    }

    // Top-level, exported definitions of `name` in the module at `path`,
    // plus whatever it re-exports.
    void collectExported(std::string const& path, std::wstring const& modName,
                         std::wstring const& name, int depth) {
        if (!visited_.insert(path + "\n" + wideToUtf8(name)).second) return;
        auto* m = load(path);
        if (!m) return;
        for (auto const& d : m->scan.defs)
            if (d.topLevel && !d.isPrivate && d.name == name)
                addHit(path, modName, d, m->text);
        std::string dir = fs::path(path).parent_path().string();
        for (auto const& e : m->scan.imports)
            if (e.isExport) visitSpec(e, dir, name, L"", depth + 1);
    }

    DefinitionQuery const& q_;
    DefinitionResult result_;
    std::map<std::string, std::unique_ptr<Module>> cache_;
    std::set<std::string> visited_;
};

}

DefinitionResult findDefinitions(DefinitionQuery const& query) {
    return Finder(query).run();
}

}
