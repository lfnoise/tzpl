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
//  synthdef_compile_link.cpp
//  synthdef-compiler
//
//  Compilation and linking functions for synthdef plugins.
//  Extracted from synthdef_compile.cpp so these are available in the library.
//

#include "synthdef_compile_link.hpp"
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <print>

namespace fs = std::filesystem;

namespace synthdef {

using SynthDefLoadFun = tzpl_SynthDef (*)();

static string synthNameSuffix = "_synth";

static int compile(string const& filepath_c, string const& filepath_o, string const& includeDir)
{
    printf("\nbegin C compile plugin =====================================================\n");

    string cmd = "clang";
    cmd += " -x c++ -arch arm64 -std=c++23 -stdlib=libc++";
    cmd += " -o " + filepath_o;
    cmd += " -O3";
    cmd += " -ffast-math";
    cmd += " -I " + includeDir;
    cmd += " -c " + filepath_c;

    printf("COMPILE: %s\n", cmd.c_str());
    FILE* pf = popen(cmd.c_str(), "r");

    while(1) {
        char buffer[2048];
        char *line = fgets(buffer, sizeof(buffer), pf);
        if (!line) break;
        printf("%s", line);
    }
    int status = pclose(pf);
    if (status) {
        printf("error %d compiling '%s'\n", status, filepath_c.c_str());
        return WEXITSTATUS(status);
    }
    printf("end C compile plugin =====================================================\n");
    return 0;
}

static int link(string const& filepath_o, string const& filepath_dylib) {
    printf("\nbegin call linker =====================================================\n");
    string cmd = "clang";
    cmd += " -arch arm64";
    cmd += " -dynamiclib";
    cmd += " -undefined dynamic_lookup";
    cmd += " -compatibility_version 1 -current_version 1";
#ifdef __APPLE__
    cmd += " -framework Accelerate";
#endif
    cmd += " -o " + filepath_dylib;
    cmd += " " + filepath_o;
    printf("LINK: %s\n", cmd.c_str());
    FILE* pf = popen(cmd.c_str(), "r");

    while(1) {
        char buffer[2048];
        char *line = fgets(buffer, sizeof(buffer), pf);
        if (!line) break;
        printf("%s", line);
    }
    int status = pclose(pf);
    if (status) {
        printf("link failed: %d\n", status);
        return WEXITSTATUS(status);
    }
    printf("end call linker =====================================================\n");
    return 0;
}

static string ensureTrailingSlash(string const& path) {
    if (path.empty() || path.back() == '/') return path;
    return path + '/';
}

string getBuildDir() {
    const char* tzpl_build = getenv("TZPL_BUILD");
    if (tzpl_build && tzpl_build[0] != '\0') {
        return ensureTrailingSlash(tzpl_build);
    }
    const char* homedir = getenv("HOME");
    if (homedir) {
        return string(homedir) + "/tzpl-build/";
    }
    return "/tmp/tzpl-build/";
}

void ensureBuildDirs(string const& buildDir) {
    fs::create_directories(buildDir + "include");
    fs::create_directories(buildDir + "cpp");
    fs::create_directories(buildDir + "obj");
    fs::create_directories(buildDir + "dylib");

#ifdef TZPL_SHARED_DIR
    string srcDir = ensureTrailingSlash(TZPL_SHARED_DIR);
    string dstDir = buildDir + "include/";
    for (auto const& entry : fs::directory_iterator(srcDir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".h" || ext == ".hpp") {
                fs::copy_file(entry.path(), dstDir + entry.path().filename().string(),
                              fs::copy_options::update_existing);
            }
        }
    }
#endif
}

void writeCodeToFile(string const& buildDir, string const& synthName, string const& ccode) {
    string filename = synthName + synthNameSuffix + ".cpp";
    string filepath = buildDir + "cpp/" + filename;
    std::println("writing code to {}", filepath);

    FILE* fp = fopen(filepath.c_str(), "w");
    if (!fp) {
        throw std::runtime_error(std::format("couldn't open output file '{}'", filepath));
    }
    auto writeSize = ccode.size();
    auto writtenSize = fwrite(ccode.c_str(), 1, writeSize, fp);
    if (writtenSize != writeSize) {
        fclose(fp);
        throw std::runtime_error("failed to write everything");
    }
    fclose(fp);
}

string dylibPath(string const& buildDir, string const& synthName) {
    return buildDir + "dylib/" + synthName + synthNameSuffix + ".dylib";
}

int compileAndLink(string const& buildDir, string const& synthName) {
    string filename = synthName + synthNameSuffix;
    string filepath_c = buildDir + "cpp/" + filename + ".cpp";
    string filepath_o = buildDir + "obj/" + filename + ".o";
    string filepath_dylib = dylibPath(buildDir, synthName);
    string includeDir = buildDir + "include";

    int err = compile(filepath_c, filepath_o, includeDir);
    if (err) return err;

    err = link(filepath_o, filepath_dylib);
    if (err) return err;

    return 0;
}

optional<tzpl_SynthDef> loadDef(std::string path) {
    const char* path_c = path.c_str();

    void* handle = dlopen(path_c, RTLD_NOW);

    if (!handle) {
        fprintf(stderr, "*** ERROR: dlopen '%s' err '%s'\n", path_c, dlerror());
        fprintf(stdout, "*** ERROR: dlopen '%s' err '%s'\n", path_c, dlerror());
        return {};
    }

    void *ptr;

    ptr = dlsym(handle, "load");
    if (!ptr) {
        fprintf(stderr, "*** ERROR: dlsym %s err '%s'\n", "load", dlerror());
        dlclose(handle);
        return {};
    }

    SynthDefLoadFun loadFunc = (SynthDefLoadFun)ptr;

    tzpl_SynthDef def = (*loadFunc)();

    return def;
}

} // namespace synthdef
