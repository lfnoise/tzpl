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
#include <print>

namespace synthdef {

using SynthDefLoadFun = tzpl_SynthDef (*)();

static int compile(string const& filepath_c, string const& filepath_o)
{
    printf("\nbegin C compile plugin =====================================================\n");

    // call compiler
    string cmd = "clang";
    cmd += " -x c++ -arch arm64 -std=c++23 -stdlib=libc++";
    cmd += " -o " + filepath_o;
    cmd += " -O3";
    cmd += " -ffast-math";
#ifdef TZPL_SHARED_DIR
    cmd += " -I " TZPL_SHARED_DIR;
#endif
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

string getBuildDir() {
    string builddir;
    const char* tzpl_build = getenv("TZPL_BUILD");
    if (tzpl_build) {
        builddir = tzpl_build;
    } else {
        const char* homedir = getenv("HOME");
        if (homedir) {
            builddir = string(homedir) + "/tzpl-build/";
        } else {
            builddir = "/tmp/";
        }
    }
    return builddir;
}

static string synthNameSuffix = "_synth";

void writeCodeToFile(string dir, string synthName, string ccode) {
    string filename = string(synthName) + synthNameSuffix;
    string filename_c = filename + ".cpp";
    string filepath_c = dir + filename_c;
    std::println("writing code to {}", filepath_c);
    {
        // write file
        FILE* fp = fopen(filepath_c.c_str(), "w");
        if (!fp) {
            throw std::runtime_error(std::format("couldn't open output file '{}'", filepath_c.c_str()));
        }
        const char* ccode_cstr = ccode.c_str();
        auto writeSize = ccode.size();
        auto writtenSize = fwrite(ccode_cstr, 1, writeSize, fp);
        if (writtenSize != writeSize) {
            throw std::runtime_error("failed to write everything");
        }
        fclose(fp);
    }
}


int compileAndLink(string dir, string synthName) {
    string filename = synthName + synthNameSuffix;
    string filepath_o = dir + filename + ".o";
    string filepath_c = dir + filename + ".cpp";
    string filepath_dylib = dir + filename + ".dylib";

    int err = compile(filepath_c, filepath_o);
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
