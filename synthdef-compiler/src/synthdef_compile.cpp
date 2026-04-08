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
//  synthdef_compile.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/5/24.
//

#include "synthdef_compile.hpp"
#include "synthdef_compile_link.hpp"
#include "synthdef_synth.hpp"
#include "synthdef_cpp_codegen.hpp"
#include "tzpl_plugin_abi.h"
#include "synthdef_audio_io.hpp"
#include <dlfcn.h> // dlopen, dlclose
#include <unistd.h>

#define GENERATE_CODE 1
#define COMPILE_CODE 1
#define RUN_INTERNAL_AUDIO_ENGINE 1
#define RUN_EXTERNAL_AUDIO_ENGINE 0

namespace synthdef {

string codegen(string synthName, std::function<void()> f)
{
    PushSynth ps(new Synth(synthName));

    f();

    gSynth->graphAnalysis();
    return cppCodeGen(gSynth);
}

static string synthNameSuffix = "_synth";

usize elemSize(tzpl_ElemType elem) {
    switch (elem) {
        case tzpl_kI32 : return sizeof(i32);
        case tzpl_kI64 : return sizeof(i64);
        case tzpl_kF32 : return sizeof(f32);
        case tzpl_kF64 : return sizeof(f64);
        default: std::unreachable();
    }
}

tzpl_SynthData* setupSynth(tzpl_SynthDef const& def, f64 fs) {
    auto funs = def.funs;

    tzpl_SynthData* synth = def.funs.alloc();
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
        tzpl_PortDef const& in = def.ins[i];
        synth->inlets[i] =  (void*)calloc(in.type.chans, elemSize(in.type.elem));
    }

    // initialize output ports
    for (int i = 0; i < synth->num_outs; ++i) {
        tzpl_PortDef const& out = def.outs[i];
        synth->outlets[i] = (void*)calloc(out.type.chans, elemSize(out.type.elem));
    }

    // initialize controls
    for (int i = 0; i < synth->num_controls; ++i) {
        tzpl_ControlDef const& ctl = def.controls[i];
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
        tzpl_SynthDef def = opt_synthdef->def;

        assert(def.num_ins == 0);
        assert(def.num_outs == 1);
        assert(def.num_controls == 0);

        tzpl_SynthData* data = def.funs.alloc();

        printf("data = %p\n", (void*)data);

        // Allocate the outlets pointer array (alloc() zeros the struct)
        data->num_outs = def.num_outs;
        data->outlets = (void**)calloc(def.num_outs, sizeof(void*));

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
        free(data->outlets);
        def.funs.free(data);
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
        string cmd = "/usr/local/bin/tzpl_audioengine ";
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
            printf("tzpl_audioengine failed: %d\n", status);
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
