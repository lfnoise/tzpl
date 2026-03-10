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

using SynthDefLoadFun = jscs_SynthDef (*)();

static int compile(string const& filepath_c, string const& filepath_o)
{
    printf("\nbegin C compile plugin =====================================================\n");

    // call compiler
    string cmd = "clang";
    cmd += " -x c++ -arch arm64 -std=c++23 -stdlib=libc++";
    cmd += " -o " + filepath_o;
    cmd += " -O3";
    cmd += " -ffast-math";
#ifdef JSCS_SHARED_DIR
    cmd += " -I " JSCS_SHARED_DIR;
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
    const char* sapf_build = getenv("SAPF3_BUILD");
    if (sapf_build) {
        builddir = sapf_build;
    } else {
        const char* homedir = getenv("HOME");
        if (homedir) {
            builddir = string(homedir) + "/sapf-build-5/";
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

optional<jscs_SynthDef> loadDef(std::string path) {
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

    jscs_SynthDef def = (*loadFunc)();

    return def;
}

} // namespace synthdef
