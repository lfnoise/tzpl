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
//  app_config.cpp
//  app
//

#include "app_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <sstream>

namespace fs = std::filesystem;

namespace tzplapp {

// ---------------------------------------------------------------------------
// The key table: one row per config key, in the order they are appended to
// a file that does not mention them yet. Exactly one member pointer is set.
// ---------------------------------------------------------------------------

namespace {

struct KeyRef {
    char const* name;
    int AppConfig::* i;
    double AppConfig::* d;
    std::string AppConfig::* s;
};

constexpr KeyRef kKeys[] = {
    {"device",            nullptr, nullptr, &AppConfig::deviceName},
    {"channels",          &AppConfig::channels, nullptr, nullptr},
    {"firstChannel",      &AppConfig::firstChannel, nullptr, nullptr},
    {"inputDevice",       nullptr, nullptr, &AppConfig::inputDeviceName},
    {"inputChannels",     &AppConfig::inputChannels, nullptr, nullptr},
    {"firstInputChannel", &AppConfig::firstInputChannel, nullptr, nullptr},
    {"sampleRate",        nullptr, &AppConfig::sampleRate, nullptr},
    {"bufferFrames",      &AppConfig::bufferFrames, nullptr, nullptr},
    {"silos",             &AppConfig::numSilos, nullptr, nullptr},
    {"tempoClocks",       &AppConfig::numTempoClocks, nullptr, nullptr},
    {"oscPort",           &AppConfig::oscPort, nullptr, nullptr},
    {"natsUrl",           nullptr, nullptr, &AppConfig::natsUrl},
    {"engineName",        nullptr, nullptr, &AppConfig::engineName},
};

std::string trim(std::string const& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string stripQuotes(std::string const& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// A string value is always quoted on write: it may be empty (clearing the
// key) or hold spaces, as device names routinely do.
std::string quote(std::string const& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string formatValue(KeyRef const& k, AppConfig const& cfg) {
    if (k.i) return std::format("{}", cfg.*(k.i));
    if (k.d) {
        double v = cfg.*(k.d);
        // 48000, not 48000.000000 -- config files are read by people.
        if (v == (double)(long long)v) return std::format("{}", (long long)v);
        return std::format("{}", v);
    }
    return quote(cfg.*(k.s));
}

// The key a line assigns to, or "" if it assigns to nothing. `commented`
// reports whether it sits behind a `--`, so a template line like
// `-- silos = 2` can be found and reused.
std::string lineKey(std::string const& line, bool& commented) {
    std::string s = trim(line);
    commented = false;
    if (s.starts_with("--")) {
        commented = true;
        s = trim(s.substr(2));
    }
    auto eq = s.find('=');
    if (eq == std::string::npos) return {};
    std::string key = trim(s.substr(0, eq));
    if (key.empty()) return {};
    for (char c : key)
        if (!(isalnum((unsigned char)c) || c == '_')) return {};
    return key;
}

}  // namespace

// ---------------------------------------------------------------------------

bool loadConfigFile(std::string const& path, AppConfig& cfg,
                    std::vector<std::string>* problems) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    auto report = [&](int line, std::string const& msg) {
        if (problems) problems->push_back(std::format("{}:{}: {}", path, line, msg));
    };

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.starts_with("--")) continue;

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            report(lineNum, "expected 'key = value'");
            continue;
        }
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));

        KeyRef const* found = nullptr;
        for (auto const& k : kKeys)
            if (key == k.name) { found = &k; break; }
        if (!found) {
            report(lineNum, std::format("unknown config key '{}'", key));
            continue;
        }

        try {
            if (found->i)      cfg.*(found->i) = std::stoi(value);
            else if (found->d) cfg.*(found->d) = std::stod(value);
            else               cfg.*(found->s) = stripQuotes(value);
        } catch (std::exception const& e) {
            report(lineNum, std::format("invalid value for '{}': {}", key, e.what()));
        }
    }
    return true;
}

bool saveConfigFile(std::string const& path, AppConfig const& cfg,
                    std::string* err) {
    auto fail = [&](std::string const& msg) {
        if (err) *err = msg;
        return false;
    };

    // Read what is there now (missing file: start from a header we own).
    std::vector<std::string> lines;
    bool existed = false;
    {
        std::ifstream in(path);
        if (in.is_open()) {
            existed = true;
            std::string line;
            while (std::getline(in, line)) lines.push_back(line);
        }
    }
    if (!existed) {
        lines.push_back("-- Tzopilotl engine configuration"
                        " (key = value, `--` comments).");
        lines.push_back("-- Read at launch: changes apply when the app"
                        " is relaunched.");
    }

    AppConfig const defaults{};
    std::vector<std::string> appended;

    for (auto const& k : kKeys) {
        std::string value = formatValue(k, cfg);
        bool isDefault = value == formatValue(k, defaults);

        // Prefer an active assignment; fall back to a commented template
        // line, which is what a freshly created project config is made of.
        int active = -1, commentedLine = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            bool commented = false;
            if (lineKey(lines[i], commented) != k.name) continue;
            if (!commented) { if (active < 0) active = i; }
            else if (commentedLine < 0) commentedLine = i;
        }

        if (active >= 0) {
            // Always rewrite an explicit setting, even back to the default:
            // silently dropping a line the user can see would be worse.
            lines[active] = std::format("{} = {}", k.name, value);
        } else if (!isDefault) {
            if (commentedLine >= 0)
                lines[commentedLine] = std::format("{} = {}", k.name, value);
            else
                appended.push_back(std::format("{} = {}", k.name, value));
        }
    }

    for (auto const& l : appended) lines.push_back(l);

    fs::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
        if (ec) return fail(std::format("cannot create {}: {}",
                                        p.parent_path().string(), ec.message()));
    }

    // Write via a temporary in the same directory, then rename: a failed
    // write must not leave the user without an engine config.
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return fail(std::format("cannot write {}", tmp.string()));
        for (auto const& l : lines) out << l << "\n";
        out.flush();
        if (!out) return fail(std::format("write failed: {}", tmp.string()));
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp);
        return fail(std::format("cannot replace {}: {}", path, ec.message()));
    }
    return true;
}

std::string userConfigFile() {
    char const* home = std::getenv("HOME");
#if defined(__APPLE__)
    if (home && *home) {
        return (fs::path(home) / "Library" / "Application Support" / "Tzopilotl"
                / "tzpl-config").string();
    }
#else
    if (char const* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return (fs::path(xdg) / "tzpl" / "tzpl-config").string();
    if (home && *home)
        return (fs::path(home) / ".config" / "tzpl" / "tzpl-config").string();
#endif
    return {};
}

}  // namespace tzplapp
