//
//  synthdef_compile.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/5/24.
//

#include "synthdef_compile.hpp"
#include "synthdef_synth.hpp"
#include "synthdef_cpp_codegen.hpp"
#include "jscs_plugin_abi.h"
#include "synthdef_audio_io.hpp"
#include <dlfcn.h> // dlopen, dlclose
#include <unistd.h>

#define GENERATE_CODE 1
#define COMPILE_CODE 1
#define RUN_INTERNAL_AUDIO_ENGINE 1
#define RUN_EXTERNAL_AUDIO_ENGINE 0

namespace synthdef {

using SynthDefLoadFun = jscs_SynthDef (*)();

int compile(string const& filepath_c, string const& filepath_o)
{
    printf("\nbegin C compile plugin =====================================================\n");
//    string filename_c = filename + ".c";
//    string filepath_o = builddir + filename + ".o";
//    string filepath_c = builddir + filename_c;

//        cmd = ['clang',
//               '-x', 'c++',
//               '-arch', 'x86_64',
//               '-std=c++23', '-stdlib=libc++',
//               '-o', self.filepath_o,
//               '-O3',
//               '-ffast-math',
//               '-c', self.filepath_c,

    // call compiler
    string cmd = "clang";
    //cmd += " -x c++ -arch x86_64 -std=c++23 -stdlib=libc++";
    cmd += " -x c++ -arch arm64 -std=c++23 -stdlib=libc++";
    //cmd += " -bundle";
    cmd += " -o " + filepath_o;
    cmd += " -O3";
    cmd += " -ffast-math";
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

int emitLLVM(string const& filepath_c, string const& filepath_llvm)
{
    printf("\nbegin emit LLVM =====================================================\n");
//    string filename_c = filename + ".c";
//    string filepath_o = builddir + filename + ".o";
//    string filepath_c = builddir + filename_c;

    printf("LLVM to %s\n", filepath_llvm.c_str());

    // call compiler
    string cmd = "clang";
    //cmd += " -x c++ -arch x86_64 -std=c++23 -stdlib=libc++";
    cmd += " -x c++ -arch arm64 -std=c++23 -stdlib=libc++";
    cmd += " -S";
    cmd += " -O3";
    cmd += " -ffast-math";
    //cmd += " -mavx2";
    cmd += " -emit-llvm";
    cmd += " -o " + filepath_llvm;
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
    printf("\nend emit LLVM =====================================================\n");
    return 0;
}

int emitASM(string const& filepath_c, string const& filepath_asm)
{
    printf("\nbegin emit ASM =====================================================\n");
//    string filename_c = filename + ".c";
//    string filepath_o = builddir + filename + ".o";
//    string filepath_c = builddir + filename_c;

    printf("LLVM to %s\n", filepath_asm.c_str());

    // call compiler
    string cmd = "clang";
    //cmd += " -x c++ -arch x86_64 -std=c++23 -stdlib=libc++";
    cmd += " -x c++ -arch arm64 -std=c++23 -stdlib=libc++";
    cmd += " -S";
    cmd += " -O3";
    cmd += " -ffast-math";
    //cmd += " -masm=intel";
    //cmd += " -mavx2";
    cmd += " -o " + filepath_asm;
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
    printf("\nend emit ASM =====================================================\n");
    return 0;
}

void link(string const& filepath_o, string const& filepath_dylib) {
    string cmd = "clang";
    //cmd += " -arch x86_64";
    cmd += " -arch arm64";
    cmd += " -dynamiclib";
    cmd += " -undefined dynamic_lookup";
//        cmd += " -Wl,";
//        cmd += "-U,_pull_input,-U,_ae_alloc_node_struct,-U,_ae_nodedef_add,";
//        cmd += "-U,_frac,-U,_fracf,-U,_nextPowerOfTwo";
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
        exit(WEXITSTATUS(status));
    }
}

optional<jscs_SynthDef> loadDef(std::string path) {
    const char* path_c = path.c_str();
    
    void* handle = dlopen(path_c, RTLD_NOW);

    if (!handle) {
        fprintf(stderr, "*** ERROR: dlopen '%s' err '%s'\n", path_c, dlerror());
        fprintf(stdout, "*** ERROR: dlopen '%s' err '%s'\n", path_c, dlerror());
        dlclose(handle);
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

string codegen(string synthName, std::function<void()> f)
{
    PushSynth ps(new Synth(synthName));

    f();

    gSynth->graphAnalysis();    
    return cppCodeGen(gSynth);
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


void compileAndLink(string dir, string synthName) {
    string filename = synthName + synthNameSuffix;
    string filename_c = filename + ".cpp";
//    string filepath_llvm = dir + filename + ".ll";
//    string filepath_asm = dir + filename + ".s";
    string filepath_o = dir + filename + ".o";
    string filepath_c = dir + filename_c;
    string filepath_dylib = dir + filename + ".dylib";
    try {
//        if (emitLLVM(filepath_c, filepath_llvm)) {
//            throw std::runtime_error("emit LLVM failed.");
//        }
//        if (emitASM(filepath_c, filepath_asm)) {
//            throw std::runtime_error("emit LLVM failed.");
//        }
        if (compile(filepath_c, filepath_o)) {
            throw std::runtime_error("compile failed.");
        }
    } catch (std::exception& err) {
        printf("error: %s\n", err.what());
        exit(1);
    } catch(...) {
        printf("unknown error. exiting..\n");
        exit(1);
    }
    
    try {
    printf("\nbegin call linker =====================================================\n");
        // call linker
        string cmd = "clang";
        //cmd += " -arch x86_64";
        cmd += " -arch arm64";
        cmd += " -dynamiclib";
        cmd += " -undefined dynamic_lookup";
//        cmd += " -Wl,";
//        cmd += "-U,_pull_input,-U,_ae_alloc_node_struct,-U,_ae_nodedef_add,";
//        cmd += "-U,_frac,-U,_fracf,-U,_nextPowerOfTwo";
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
            exit(WEXITSTATUS(status));
        }
    printf("end call linker =====================================================\n");
    } catch (std::exception& err) {
        printf("error: %s\n", err.what());
        exit(1);
    } catch(...) {
        printf("unknown error. exiting..\n");
        exit(1);
    }

}

usize elemSize(jscs_ElemType elem) {
    switch (elem) {
        case jscs_kI32 : return sizeof(i32);
        case jscs_kI64 : return sizeof(i64);
        case jscs_kF32 : return sizeof(f32);
        case jscs_kF64 : return sizeof(f64);
        default: std::unreachable();
    }
}

jscs_SynthData* setupSynth(jscs_SynthDef const& def, f64 fs) {
    auto funs = def.funs;
    
    jscs_SynthData* synth = def.funs.alloc();
    synth->engine = nullptr;
    synth->node = nullptr;
    synth->funs = funs;
    
    synth->num_ins = def.num_ins;
    synth->num_outs = def.num_outs;
    synth->num_controls = def.num_controls;
    
    synth->inlets = (void**)calloc(synth->num_ins, sizeof(void*));
    synth->outlets = (void**)calloc(synth->num_outs, sizeof(void*));
    synth->controls = (void**)calloc(synth->num_controls, sizeof(void*));
    synth->fs = fs;
    synth->sd = 1. / synth->fs;
    
    // initialize input ports
    for (int i = 0; i < synth->num_ins; ++i) {    
        jscs_PortDef const& in = def.ins[i];
        synth->inlets[i] =  (void*)calloc(in.type.chans, elemSize(in.type.elem));
    }
    
    // initialize output ports
    for (int i = 0; i < synth->num_outs; ++i) {    
        jscs_PortDef const& out = def.outs[i];
        synth->outlets[i] = (void*)calloc(out.type.chans, elemSize(out.type.elem));
    }

    // initialize controls
    for (int i = 0; i < synth->num_controls; ++i) {    
        jscs_ControlDef const& ctl = def.controls[i];
        synth->controls[i] = (void*)calloc(ctl.type.chans, elemSize(ctl.type.elem));
    }
    
    return synth;
}


void runInternalAudioEngine(string dir, string synthName, int seconds) {
    string filename = synthName + synthNameSuffix;
    string filepath_dylib = dir + filename + ".dylib";
    try {
        printf("\nbegin run audio engine =====================================================\n");
        auto opt_synthdef = loadDef(filepath_dylib);
        if (!opt_synthdef.has_value()) {
            printf("load synthdef failed. exiting..\n");
            exit(1);
        }
        jscs_SynthDef def = opt_synthdef.value();

        assert(def.num_ins == 0);
        assert(def.num_outs == 1);
        assert(def.num_controls == 0);
        
        jscs_SynthData* data = def.funs.alloc();
        
        printf("data = %p\n", (void*)data);
        printf("data->outlets = %p\n", (void*)data->outlets);
        
        f32 outs[2];
        data->outlets[0] = outs;
        data->fs = 48000;
        
        def.funs.init(data);

        AudioEngine e;
        
        e.streamParams = { "default", 2, 0, 256, 48000.};

        e.processFun = [&](AudioEngine* e, f32* out, f32 const* in, int numFrames) {
            int chans = e->streamParams.channels;
            for (int i = 0, j = 0; i < numFrames; ++i, j += chans) {
                def.funs.processAudio(data);
                out[j] = outs[0];
                out[j+1] = outs[1];
            }
        };
        
        initAudio(&e);
        std::println("NOW PLAYING: {}", synthName);
        startAudio(&e);
        sleep(seconds);
        stopAudio(&e);
        uninitAudio(&e);
    } catch (std::exception& err) {
        printf("error: %s\n", err.what());
        exit(1);
    } catch(...) {
        printf("unknown error. exiting..\n");
        exit(1);
    }
}


void test(string synthName, int seconds, std::function<void()> f)
{
    printf("TEST: %s ================================\n", synthName.c_str());

    string ccode;
    try {
        PushSynth ps(new Synth(synthName));

        f();

        printf("GRAPH ANALYSIS\n");
        gSynth->graphAnalysis();
        
        gSynth->dump();
#if GENERATE_CODE
        printf("CODE GEN\n");
        ccode = cppCodeGen(gSynth);
        std::print("{}\n", ccode);
#endif
    } catch (std::exception& err) {
        printf("error: %s\n", err.what());
        exit(1);
    } catch(...) {
        printf("unknown error. exiting..\n");
        exit(1);
    }

#if GENERATE_CODE
#if COMPILE_CODE
    string dir = getBuildDir();
    writeCodeToFile(dir, synthName, ccode);
    compileAndLink(dir, synthName);

#if RUN_INTERNAL_AUDIO_ENGINE
    runInternalAudioEngine(dir, synthName, seconds);
#endif // RUN_INTERNAL_AUDIO_ENGINE
#if RUN_EXTERNAL_AUDIO_ENGINE
    try {
    printf("\nbegin run audio engine =====================================================\n");
        string cmd = "/usr/local/bin/sapf_audioengine5 ";
        cmd += synthName;
        cmd += " ";
        cmd += filepath_dylib;
        // try to load the dylib into the audio engine
        printf("RUN: %s\n", cmd.c_str());
        FILE* pf = popen(cmd.c_str(), "r");
        
        while(1) {
            char buffer[2048];
            char *line = fgets(buffer, sizeof(buffer), pf);
            if (!line) break;
            printf("%s", line);
        }
        int status = pclose(pf);
        if (status) {
            printf("sapf_audioengine5 failed: %d\n", status);
            exit(1);
            //exit(WEXITSTATUS(status));
        }
    printf("\nend run audio engine =====================================================\n");
    } catch (std::exception& err) {
        printf("error: %s\n", err.what());
        exit(1);
    } catch(...) {
        printf("unknown error. exiting..\n");
        exit(1);
    }
#endif // RUN_EXTERNAL_AUDIO_ENGINE
#endif // COMPILE_CODE
#endif // GENERATE_CODE

}

}
