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
//  app_config_test.cpp
//  app
//
//  Headless tests for the tzpl-config reader/writer behind the startup
//  path and the Engine Settings dialog: layering, diagnostics, and the
//  in-place save that has to keep the user's comments intact.
//

#include "app_config.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace tzplapp;

static int gPassed = 0, gFailed = 0;

static void check(bool cond, std::string_view what) {
    if (cond) { std::print("  PASS: {}\n", what); ++gPassed; }
    else      { std::print("  FAIL: {}\n", what); ++gFailed; }
}

static fs::path tmpDir() {
    fs::path d = fs::temp_directory_path() / "tzpl_config_test";
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

static void write(fs::path const& p, std::string const& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

static std::string read(fs::path const& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool has(std::string const& hay, std::string const& needle) {
    return hay.find(needle) != std::string::npos;
}

// ---------------------------------------------------------------------------

static void test_load() {
    std::print("Test: loading a config file\n");
    auto dir = tmpDir();
    auto cfg = dir / "tzpl-config";
    write(cfg,
          "-- a project config\n"
          "\n"
          "silos = 2\n"
          "inputChannels = 2\n"
          "device = \"Scarlett 2i2\"\n"
          "sampleRate = 44100\n"
          "-- bufferFrames = 1024\n"
          "nonsense\n"
          "bogusKey = 3\n"
          "channels = twelve\n");

    AppConfig c;
    std::vector<std::string> problems;
    check(loadConfigFile(cfg.string(), c, &problems), "file opened");
    check(c.numSilos == 2 && c.inputChannels == 2, "ints read");
    check(c.deviceName == "Scarlett 2i2", "quoted string unquoted");
    check(c.sampleRate == 44100.0, "double read");
    check(c.bufferFrames == 512, "commented line ignored (default kept)");
    check(c.channels == 2, "unparsable value leaves the default");
    check(problems.size() == 3, "three diagnostics (syntax, unknown key, value)");
    check(has(problems[1], "unknown config key 'bogusKey'"), "unknown key named");

    AppConfig missing;
    check(!loadConfigFile((dir / "nope").string(), missing, nullptr),
          "absent file reports false");
}

static void test_save_preserves_file() {
    std::print("Test: saving keeps comments, order, and unknown keys\n");
    auto dir = tmpDir();
    auto cfg = dir / "tzpl-config";
    write(cfg,
          "-- Tzopilotl project config\n"
          "-- silos = 2\n"
          "silos = 3\n"
          "futureKey = 1\n");

    AppConfig c;
    loadConfigFile(cfg.string(), c);
    c.numSilos = 6;
    c.inputChannels = 2;         // not mentioned in the file: appended
    c.deviceName = "Studio 68c";
    std::string err;
    check(saveConfigFile(cfg.string(), c, &err), "save succeeded");

    std::string text = read(cfg);
    check(has(text, "-- Tzopilotl project config"), "header comment survives");
    check(has(text, "futureKey = 1"), "unknown key survives");
    check(has(text, "silos = 6"), "existing key updated in place");
    check(!has(text, "silos = 3"), "old value gone");
    check(has(text, "-- silos = 2"),
          "a commented template line next to an active one is left alone");
    check(has(text, "inputChannels = 2"), "new non-default key appended");
    check(has(text, "device = \"Studio 68c\""), "string value quoted");
    check(!has(text, "tempoClocks"),
          "untouched defaults are not written out");

    // Reading it back must reproduce exactly what was saved.
    AppConfig back;
    loadConfigFile(cfg.string(), back);
    check(back.numSilos == 6 && back.inputChannels == 2
              && back.deviceName == "Studio 68c",
          "round trip");
}

static void test_save_uncomments_template() {
    std::print("Test: saving reuses a commented template line\n");
    auto dir = tmpDir();
    auto cfg = dir / "tzpl-config";
    // Exactly what File > New Project writes.
    write(cfg,
          "-- Tzopilotl project config (key = value, `--` comments).\n"
          "-- silos = 2\n"
          "-- sampleRate = 48000\n"
          "-- bufferFrames = 512\n"
          "-- channels = 2\n");

    AppConfig c;
    loadConfigFile(cfg.string(), c);
    c.numSilos = 8;
    check(saveConfigFile(cfg.string(), c), "save succeeded");

    std::string text = read(cfg);
    check(has(text, "silos = 8") && !has(text, "-- silos = 2"),
          "template line becomes the real setting");
    check(has(text, "-- sampleRate = 48000"),
          "untouched template lines stay commented");
}

static void test_save_creates_file() {
    std::print("Test: saving creates a missing file and its directory\n");
    auto dir = tmpDir();
    auto cfg = dir / "nested" / "deeper" / "tzpl-config";

    AppConfig c;
    c.inputChannels = 2;
    std::string err;
    check(saveConfigFile(cfg.string(), c, &err), "save succeeded: " + err);
    check(fs::exists(cfg), "file created");

    std::string text = read(cfg);
    check(has(text, "inputChannels = 2"), "value written");
    check(has(text, "--"), "created file carries an explanatory header");

    AppConfig back;
    loadConfigFile(cfg.string(), back);
    check(back.inputChannels == 2, "round trip");
    check(back.numSilos == AppConfig{}.numSilos, "defaults untouched");
}

static void test_explicit_default_is_kept() {
    std::print("Test: an explicit setting stays even at the default value\n");
    auto dir = tmpDir();
    auto cfg = dir / "tzpl-config";
    write(cfg, "inputChannels = 2\n");

    AppConfig c;
    loadConfigFile(cfg.string(), c);
    c.inputChannels = 0;  // the user turns input back off
    check(saveConfigFile(cfg.string(), c), "save succeeded");
    check(has(read(cfg), "inputChannels = 0"),
          "line the user can see is rewritten, not dropped");
}

static void test_layering() {
    std::print("Test: project settings override the user's\n");
    auto dir = tmpDir();
    auto user = dir / "user-config";
    auto proj = dir / "tzpl-config";
    write(user, "inputChannels = 2\nsilos = 8\n");
    write(proj, "silos = 3\n");

    AppConfig c;
    loadConfigFile(user.string(), c);
    loadConfigFile(proj.string(), c);
    check(c.numSilos == 3, "project wins where both set a key");
    check(c.inputChannels == 2, "user setting survives where the project is silent");
}

int main() {
    std::print("=== App config tests ===\n\n");
    test_load();
    test_save_preserves_file();
    test_save_uncomments_template();
    test_save_creates_file();
    test_explicit_default_is_kept();
    test_layering();
    fs::remove_all(fs::temp_directory_path() / "tzpl_config_test");
    std::print("\n=== {} passed, {} failed ===\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
