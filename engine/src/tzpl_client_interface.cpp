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
//  tzpl_client_interface.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_engine.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_node.hpp"
#include "RtAudio.h"
#include "tzpl_hash.hpp"
#include "tzpl_command_subclasses.hpp"
#include <cstring>
#include <dlfcn.h> // dlopen, dlclose
#include <filesystem>
#include <chrono>
#include <thread>

namespace engine {

//=============================================================================================
#pragma mark CLIENT INTERFACE IMPLEMENTATION

void uninitAudio(Engine* e);

Engine* newEngine(EngineConfig const& config, AudioStreamParameters& asp) {
#if DEBUG_NODES
    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    numNodesCreated = 0;
    numNodesDeleted = 0;
#endif
    return new Engine(config, asp);
}

void freeEngine(Engine* e) {
    uninitAudio(e);

    delete e;
#if DEBUG_NODES
    printf("numNodesCreated %d\n", numNodesCreated);
    printf("numNodesDeleted %d\n", numNodesDeleted);
    printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
#endif
}

RtAudio* getRTAudio(Engine* e) {
    return e->rtaudio_.get();
}


bool loadOneDef(Engine* e, const char* path) {
    void* handle = dlopen(path, RTLD_NOW);

    if (!handle) {
        fprintf(stderr, "*** ERROR: dlopen '%s' err '%s'\n", path, dlerror());
        fprintf(stdout, "*** ERROR: dlopen '%s' err '%s'\n", path, dlerror());
        dlclose(handle);
        return false;
    }

    void *ptr;

    ptr = dlsym(handle, "load");
    if (!ptr) {
        fprintf(stderr, "*** ERROR: dlsym %s err '%s'\n", "load", dlerror());
        dlclose(handle);
        return false;
    }

    LoadNodeDefFun loadFunc = (LoadNodeDefFun)ptr;

    (*loadFunc)(e);
    return true;
}

#ifdef __APPLE__
constexpr auto kPluginExt = ".dylib";
#else
constexpr auto kPluginExt = ".so";
#endif

bool loadDef(Engine* e, const char* dirPath, const char* defName) {
    namespace fs = std::filesystem;
    std::string defNameStr = std::string(defName) + "_synth";
    auto iter = fs::recursive_directory_iterator(dirPath);
    for(auto& p : iter) {
        if (p.is_regular_file()
            && fs::path(p.path()).extension() == kPluginExt
            && fs::path(p.path()).stem() == defNameStr)
        {
            bool ok = loadOneDef(e, p.path().c_str());
            if (ok) return true;
        }
    }

    return false; // not found
}

bool loadDefs(Engine* e, const char* dirPath) {
    namespace fs = std::filesystem;
    bool allOK = true;
    bool anyOK = false;
    auto iter = fs::recursive_directory_iterator(dirPath);
    for(auto& p : iter) {
        if (p.is_regular_file() && fs::path(p.path()).extension() == kPluginExt) {
            bool ok = loadOneDef(e, p.path().c_str());
            allOK = allOK && ok;
            anyOK = anyOK || ok;
        }
    }
    
    return allOK && anyOK; // all loads succeeded and there was at least one.
}

void errorCallback( RtAudioErrorType type, const std::string &errorText ) {
    printf("error %d '%s'\n", type, errorText.c_str());
}

void masterGain(Engine* e, f32 gain) {
    e->masterGain_ = gain;
}

void safetyLimiter(Engine* e, Enable onoff) {
    e->enableSafetyLimiter_ = onoff;
}

int audioCallback( void *outputBuffer, void *inputBuffer,
                    unsigned int numFrames,
                    double streamTime,
                    RtAudioStreamStatus status,
                    void *userData )
{
    Engine* e = (Engine*)userData;
    
    try {
        f32* out = (f32*)outputBuffer;

        // For duplex mode, inputBuffer comes from the callback.
        // For separate input device, use the staging buffer.
        e->in_ = e->inputRtaudio_
            ? (f32 const*)e->inputStagingBuf_
            : (f32 const*)inputBuffer;
        e->out_ = out;
        e->anchorStreamTime_ = streamTime;
        
        // release silos to work, then .
        // then mix all together.
        int numSilos = int(e->silos_.size());
        for (int i = 1; i < numSilos; ++i) {
            e->silos_[i].start_sem_.signal();
        }

        int numSamples = numFrames * e->streamParams_.channels;
        memset(out, 0, sizeof(f32) * numSamples);

        e->silos_[0].processFrames(); // silo 0 is done on the audio callback thread.
        //printf("%f %f\n", out[0], out[1]);
        f32 gain = e->masterGain_ * e->muteGain_;
        e->safetyLimiter_->process(out, e->enableSafetyLimiter_, gain);
        e->anchorSampleTime_ += numFrames;
    } catch (...) {
        fprintf(stderr, "exception on real time thread");
    }
    return 0;
}


// Find a device ID by name on a given RtAudio instance.
// Returns -1 if not found.
int findDeviceByName(RtAudio* rta, const char* name) {
    int n = rta->getDeviceCount();
    for (int i = 0; i < n; ++i) {
        auto info = rta->getDeviceInfo(i);
        if (name == info.name) {
            return i;
        }
    }
    return -1;
}

// Callback for a separate input-only stream.
// Copies hardware input data into the engine's staging buffer.
// NOTE: No clock drift compensation -- the input and output devices run on
// independent sample clocks, so over time samples will be repeated or dropped.
// For drift-free operation, use an aggregate device (macOS) or JACK (Linux).
// A future improvement would be an adaptive resampler with a PLL to track the
// rate difference.
int inputAudioCallback(void* /*outputBuffer*/, void* inputBuffer,
                       unsigned int numFrames,
                       double /*streamTime*/,
                       RtAudioStreamStatus /*status*/,
                       void* userData)
{
    Engine* e = (Engine*)userData;
    if (inputBuffer && e->inputStagingBuf_) {
        memcpy(e->inputStagingBuf_, inputBuffer,
               numFrames * e->streamParams_.inputChannels * sizeof(f32));
    }
    return 0;
}

void initAudio(Engine* e) {
    auto& rta = e->rtaudio_;

    int numDevices = rta->getDeviceCount();
    if (numDevices == 0) {
        throw tzpl_errNoAudioDevices;
    }

    // --- Resolve output device ---
    int outputDeviceID = -1;
    if (strcmp(e->streamParams_.deviceName, "default") == 0) {
        outputDeviceID = rta->getDefaultOutputDevice();
    } else {
        outputDeviceID = findDeviceByName(rta.get(), e->streamParams_.deviceName);
        if (outputDeviceID < 0) {
            throw tzpl_errDeviceNotFound;
        }
    }

    RtAudio::StreamParameters outputParams;
    outputParams.deviceId = outputDeviceID;
    outputParams.nChannels = e->streamParams_.channels;
    outputParams.firstChannel = e->streamParams_.firstChannel;

    // --- Determine input configuration ---
    int inputChannels = e->streamParams_.inputChannels;
    bool hasInput = inputChannels > 0;
    bool separateInputDevice = false;

    if (hasInput) {
        const char* inputDevName = e->streamParams_.inputDeviceName;
        bool inputIsDefault = !inputDevName || strlen(inputDevName) == 0;
        bool inputSameAsOutput = inputIsDefault
            || strcmp(inputDevName, e->streamParams_.deviceName) == 0;

        if (!inputSameAsOutput) {
            // Different input device -- need a separate RtAudio instance
            separateInputDevice = true;
        }
    }

    void* userData = (void*)e;

    if (hasInput && !separateInputDevice) {
        // --- Duplex mode: same device for input and output ---
        RtAudio::StreamParameters inputParams;
        inputParams.deviceId = outputDeviceID;
        inputParams.nChannels = inputChannels;
        inputParams.firstChannel = e->streamParams_.firstInputChannel;

        rta->setErrorCallback(errorCallback);
        unsigned int bufferFrames = e->streamParams_.bufferFrames;
        rta->openStream(&outputParams, &inputParams, RTAUDIO_FLOAT32,
                        e->streamParams_.sampleRate, &bufferFrames,
                        audioCallback, userData);

        e->streamParams_.bufferFrames = bufferFrames;
        e->streamParams_.channels = outputParams.nChannels;
        e->streamParams_.inputChannels = inputParams.nChannels;
    } else {
        // --- Output-only stream (or separate input device) ---
        rta->setErrorCallback(errorCallback);
        unsigned int bufferFrames = e->streamParams_.bufferFrames;
        rta->openStream(&outputParams, nullptr, RTAUDIO_FLOAT32,
                        e->streamParams_.sampleRate, &bufferFrames,
                        audioCallback, userData);

        e->streamParams_.bufferFrames = bufferFrames;
        e->streamParams_.channels = outputParams.nChannels;

        if (separateInputDevice) {
            // --- Open a separate input-only stream ---
            const char* inputDevName = e->streamParams_.inputDeviceName;

#ifdef __APPLE__
            e->inputRtaudio_ = std::make_unique<RtAudio>(RtAudio::MACOSX_CORE);
#elif defined(__linux__)
            e->inputRtaudio_ = std::make_unique<RtAudio>(RtAudio::LINUX_ALSA);
#endif
            e->inputRtaudio_->setErrorCallback(errorCallback);

            int inputDeviceID = -1;
            if (strcmp(inputDevName, "default") == 0) {
                inputDeviceID = e->inputRtaudio_->getDefaultInputDevice();
            } else {
                inputDeviceID = findDeviceByName(e->inputRtaudio_.get(), inputDevName);
                if (inputDeviceID < 0) {
                    throw tzpl_errDeviceNotFound;
                }
            }

            RtAudio::StreamParameters inputParams;
            inputParams.deviceId = inputDeviceID;
            inputParams.nChannels = inputChannels;
            inputParams.firstChannel = e->streamParams_.firstInputChannel;

            unsigned int inputBufFrames = e->streamParams_.bufferFrames;
            e->inputRtaudio_->openStream(nullptr, &inputParams, RTAUDIO_FLOAT32,
                                         e->streamParams_.sampleRate, &inputBufFrames,
                                         inputAudioCallback, userData);

            e->streamParams_.inputChannels = inputParams.nChannels;

            // Allocate staging buffer for the input callback to write into
            e->inputStagingBuf_ = (f32*)calloc(
                e->streamParams_.bufferFrames * inputChannels, sizeof(f32));
        }
    }

    int byteSize = e->streamParams_.bufferFrames * e->streamParams_.channels * sizeof(f32);

    e->safetyLimiter_ = std::make_unique<SafetyLimiter>(
        e->streamParams_.bufferFrames,
        e->streamParams_.channels,
        int((.25 * e->streamParams_.sampleRate) / e->streamParams_.bufferFrames));

    for (Silo& s : e->silos_) {
        s.sampleTime_ = 0;
        if (s.index_ > 0) {
            s.outbuf_ = (f32*)malloc(byteSize);
        }
    }
    e->audioState_ = AudioState::initted;
}

void uninitAudio(Engine* e) {
    stopAudio(e); // in case it was running.

    if (e->audioState_ == AudioState::off) return;

    for (Silo& s : e->silos_) {
        if (s.index_ > 0) {
            free(s.outbuf_);
            s.outbuf_ = nullptr;
        }
    }

    e->safetyLimiter_.reset();

    // Close separate input stream if present
    if (e->inputRtaudio_) {
        if (e->inputRtaudio_->isStreamOpen()) {
            e->inputRtaudio_->closeStream();
        }
        e->inputRtaudio_.reset();
    }
    free(e->inputStagingBuf_);
    e->inputStagingBuf_ = nullptr;

    e->rtaudio_->closeStream();
    e->audioState_ = AudioState::off;
}

void startAudio(Engine* e) {
    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    if (e->audioState_ == AudioState::running) return;
    if (e->audioState_ != AudioState::initted) {
        throw tzpl_errAudioNotInitialized;
    }
    e->muteGain_ = 1.f;
    if (e->inputRtaudio_) {
        e->inputRtaudio_->startStream();
    }
    e->rtaudio_->startStream();
    e->audioState_ = AudioState::running;
}

void stopAudio(Engine* e) {
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    if (e->audioState_ != AudioState::running) return;
    e->muteGain_ = 0.f;
    std::this_thread::sleep_for(std::chrono::microseconds(100000));
    e->rtaudio_->stopStream();
    if (e->inputRtaudio_) {
        e->inputRtaudio_->stopStream();
    }
    e->audioState_ = AudioState::initted;
}

void printDevices(Engine* e) 
{
    auto& rta = e->rtaudio_;
    int n = rta->getDeviceCount();
    for (int i = 0; i < n; ++i) {
        auto info = rta->getDeviceInfo(i);
        printf("%2d device: '%s'\n", i, info.name.c_str());
        printf("   %2d output ch, %2d input ch, %2d max duplex ch.\n",
            info.outputChannels, info.inputChannels, info.duplexChannels);
        if (info.isDefaultOutput) printf("   * This is the default output device.\n");
        if (info.isDefaultInput)  printf("   * This is the default input device.\n");
        printf("   sample rates: ");
        { int i = 0; for (auto sr : info.sampleRates) {
            if (i>0) {
                printf(", ");
                if ((i%8)==0) printf("\n   ");
            }
            printf("%d", sr);
            ++i;
        }}
        printf("\n");
        printf("   preferred sample rate: %d\n", info.preferredSampleRate);
    }
}


bool isAudioInitialized(Engine* e) { return e->isAudioInitialized(); }
bool isAudioRunning(Engine* e) { return e->isAudioRunning(); }

f64 getStreamTime(Engine* e) { return e->getStreamTime(); }

void hexdump(const void *addr, i32 len)
{
    printf("hexdump  addr %p  len %d\n", addr, len);
    const u8 *p = (u8 *)addr;
    u32 offset = 0;
    
    //if (len > 0x400) len = 0x400;
    
    while (len > 0) {
        int n = len > 16 ? 16 : len;
        printf("%08X: ", offset);
        for (u32 i = 0; i < 16; ++i) {
            if (i % 4 == 0) printf(" ");

            if (i < n)
                printf("%02X ", p[i]);
            else printf("   ");
        }
        char hexstr[18];
        char* s = hexstr;
        for (u32 i = 0; i < 16; ++i) {
            if (i < n) *s++ = isprint(p[i]) ? p[i] : '.';
            else *s++ = ' ';
        }
        *s = 0;
        printf(" %s\n", hexstr);
        p += 16;
        len -= 16;
        offset += 16;
    }
}

void addNodeDef(Engine* e, NodeDefInfo const& info, void* dlHandle) {
    NodeDef* def = new NodeDef(info);
    def->dlHandle_ = dlHandle;
    u32 bin = def->hash_ & kHashMask;

    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    def->next_ = e->defs_[bin];
    e->defs_[bin] = def;

    // Mark any older def with the same name as superseded.
    NodeDef* prev = def;
    NodeDef* old = def->next_;
    while (old) {
        if (old->hash_ == def->hash_
            && strcmp(old->info_.name, def->info_.name) == 0
            && !old->superseded_) {
            old->superseded_ = true;
            if (old->refCount_ == 0) {
                // No live nodes -- remove from chain and clean up now.
                prev->next_ = old->next_;
                if (old->dlHandle_) dlclose(old->dlHandle_);
                delete old;
            }
            break;
        }
        prev = old;
        old = old->next_;
    }
}

void releaseNodeDef(Engine* e, NodeDef* def) {
    // Caller must hold nrt_lock_ (or be in single-threaded shutdown).
    def->refCount_--;
    if (def->refCount_ == 0 && def->superseded_) {
        // Remove from hash chain.
        u32 bin = def->hash_ & kHashMask;
        NodeDef* prev = nullptr;
        NodeDef* cur = e->defs_[bin];
        while (cur) {
            if (cur == def) {
                if (prev) prev->next_ = cur->next_;
                else e->defs_[bin] = cur->next_;
                break;
            }
            prev = cur;
            cur = cur->next_;
        }
        if (def->dlHandle_) dlclose(def->dlHandle_);
        delete def;
    }
}

NodeDef* getNodeDef(Engine* e, const char* name) {
    u64 hash = hash64(name, kHashStart);
    u32 bin = hash & kHashMask;
    
    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    NodeDef* def = e->defs_[bin];
    while (def) {
        if (def->hash_ == hash && strcmp(def->info_.name, name) == 0) return def;
        def = def->next_;
    }
    return nullptr;
}

void addSynthDef(Engine* e, tzpl_SynthDef const& def, void* dlHandle) {
    // Build a NodeDefInfo from the tzpl_SynthDef.
    NodeDefInfo info{};
    info.name = def.name;
    info.funs = def.funs;
    info.num_ins = def.num_ins;
    info.num_outs = def.num_outs;
    info.num_controls = def.num_controls;

    // tzpl_PortDef and PortInfo have identical layout: { const char* name; tzpl_SignalType type; }
    info.ins  = reinterpret_cast<PortInfo*>(def.ins);
    info.outs = reinterpret_cast<PortInfo*>(def.outs);

    // tzpl_ControlDef → ControlInfo requires field mapping.
    if (def.num_controls > 0) {
        auto* controls = new ControlInfo[def.num_controls];
        for (int i = 0; i < def.num_controls; ++i) {
            tzpl_ControlDef const& src = def.controls[i];
            controls[i].PortInfo::name = src.name;
            controls[i].PortInfo::type = src.type;
            controls[i].name = src.name;
            controls[i].type = src.type;
            controls[i].controlID = static_cast<i64>(src.id);
            controls[i].spec = src.spec;
        }
        info.controls = controls;
    } else {
        info.controls = nullptr;
    }

    addNodeDef(e, info, dlHandle);
}

void listNodeDefs(Engine* e, std::vector<std::string>& names) {
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    for (u32 bin = 0; bin < kHashBins; ++bin) {
        NodeDef* def = e->defs_[bin];
        while (def) {
            names.emplace_back(def->info_.name);
            def = def->next_;
        }
    }
}

struct CmdBundle : CommandList
{
    Engine* engine = nullptr;
    Silo* silo = nullptr;
};

thread_local CmdBundle tBundle;

void sendCmds(Engine* e, Silo* s, Command* cmd) noexcept {
    if (e->isAudioRunning()) {
        s->from_nrt_.push(cmd);
    } else {
        while (cmd) {
            Command* next = cmd->next_;
            while (!cmd->run(s)) {} // run each command until done.
            delete cmd;
            cmd = next;
        }
    }
}

void sendCommand(Command* cmd) {
    tBundle.add(cmd);
}

tzpl_SErr begin(Engine* e, int silo) {
    if (tBundle.head) {
        return tzpl_errCommandsQueuedButNotSent;
    }
    if (silo < 0 || silo >= e->silos_.size()) {
        return tzpl_errSiloOutOfRange;
    }
    
    tBundle.engine = e;
    tBundle.silo = &e->silos_[silo];
    return tzpl_errNone;
}

tzpl_SErr sched(f64 time, SchedPolicy policy) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;

    //printf("SCHED %f\n", time);
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    Command* cmds = tBundle.popAll();
    if (!cmds) return tzpl_errNone; // not an error, just don't do anything.
    
    Command* cmd = cmds;
    while (cmd) {
        cmd->schedPolicy_ = policy;
        cmd->streamTime_ = time;
        cmd = cmd->next_;
    }
    sendCmds(e, tBundle.silo, cmds);
    tBundle.engine = nullptr;
    return tzpl_errNone;
}

tzpl_SErr go() { 
    return sched(0., schedImmediate);
}

tzpl_SErr newNode(const char* name, i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;

    NodeDef* def = getNodeDef(e, name); // getNodeDef has its own lock.
    if (!def) return tzpl_errNodeDefNotFound;

    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    
    Node* test = tBundle.silo->nrt_getNode(nodeID);
    if (test) return tzpl_errNodeIDAlreadyTaken;

    Node* node = nullptr;
    try {
        node = new Node(e, tBundle.silo, def, nodeID);
    } catch (tzpl_SErr& err) {
        printf("newNode failed %d\n", err);
        return err;
    }
    
    tBundle.add(new AddNodeCmd{node});
    return tzpl_errNone;
}

tzpl_SErr freeNode(i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    tBundle.add(new RemoveNodeCmd{nodeID});
    return tzpl_errNone;
}

tzpl_SErr freeAllNodes() {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    tBundle.add(new RemoveAllNodesCmd{});
    return tzpl_errNone;
}

tzpl_SErr channelOffset(i32 offset) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;

    tBundle.add(new ChannelOffsetCmd{offset});
    return tzpl_errNone;
}

// TODO notes, controls, buffers...

tzpl_SErr connect(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;

    tzpl_SErr err;
    OutPort* srcPort;
    err = tBundle.silo->nrt_getOutPort(src, srcPort);
    if (err != tzpl_errNone) return err;

    InPort* dstPort;
    err = tBundle.silo->nrt_getInPort(dst, dstPort);
    if (err != tzpl_errNone) return err;

    if (dstPort->node_->nodeID == 0) {
        tzpl_SErr err = relaxedCompatibleTypes(srcPort->type_, dstPort->type_);
        if (err != tzpl_errNone) return err;
    } else {
        tzpl_SErr err = compatibleTypes(srcPort->type_, dstPort->type_);
        if (err != tzpl_errNone) return err;
    }

    // Always pre-allocate a mixer in NRT (cheap). The RT thread decides
    // whether to use it based on the actual connection state at execution
    // time. This avoids NRT/RT race conditions -- the NRT thread cannot
    // reliably know whether previous connects have been applied yet.
    //
    // Use the SOURCE's type for the mixer, not the destination's. The source
    // determines the actual channel count; the mixer-to-destination connect
    // uses relaxed type checking (for the output node) which allows channel
    // mismatch. If we used the destination's type, connecting a mono source
    // to a stereo mixer input would fail strict type checking.
    tzpl_SignalType type = srcPort->type_;
    Node* mixerNode = newMixerNode(e, tBundle.silo, type, 4);

    Node* xfaderNode = nullptr;
    if (xfadeTime > 0. && isFloat(srcPort->type_.elem)) {
        xfaderNode = newXFaderNode(e, tBundle.silo, xfadeTime, curve, srcPort->type_);
    }
    tBundle.add(new ConnectCmd(src, dst, xfaderNode, curve, mixerNode, -1));
    return tzpl_errNone;
}

tzpl_SErr reconnectOutput(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    tzpl_SErr err;
    OutPort* oldSrcPort;
    err = tBundle.silo->nrt_getOutPort(oldSrc, oldSrcPort);
    if (err != tzpl_errNone) return err;
    
    OutPort* newSrcPort;
    err = tBundle.silo->nrt_getOutPort(newSrc, newSrcPort);
    if (err != tzpl_errNone) return err;

    err = compatibleTypes(oldSrcPort->type_, newSrcPort->type_);
    if (err != tzpl_errNone) return err;

    Node* xfaderNode = nullptr;
    tzpl_SignalType type = newSrcPort->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, tBundle.silo, xfadeTime, curve, type);
    }
    tBundle.add(new ReconnectOutputCmd(oldSrc, newSrc, xfaderNode, curve));
    return tzpl_errNone;
}

tzpl_SErr replaceNode(i64 oldNodeID, i64 newNodeID, f64 xfadeTime, FadeCurve curve)
{
    Silo* s = tBundle.silo;
    Node* oldNode = s->nrt_getNode(oldNodeID);
    if (!oldNode) return tzpl_errNodeNotFound;
    Node* newNode = s->nrt_getNode(newNodeID);
    if (!newNode) return tzpl_errNodeNotFound;

    if (oldNode->ins.size() != newNode->ins.size()) return tzpl_errNumPortsMismatch;
    if (oldNode->outs.size() != newNode->outs.size()) return tzpl_errNumPortsMismatch;
    
    for (int i = 0; i < oldNode->ins.size(); ++i) {
        tzpl_SErr err = compatibleTypes(oldNode->ins[i].type_, newNode->ins[i].type_);
        if (err) return err;
    }
    for (int i = 0; i < oldNode->outs.size(); ++i) {
        tzpl_SErr err = compatibleTypes(oldNode->outs[i].type_, newNode->outs[i].type_);
        if (err) return err;
    }
    
    // Connect new node to all the same inputs.
    // If an input has a hidden mixer, iterate the mixer's actual sources
    // and connect each one. The additive connect() will automatically
    // create a new mixer on the new node's input.
    for (int i = 0; i < (int)oldNode->ins.size(); ++i) {
        if (oldNode->ins[i].mixerNode_) {
            Node* mixer = oldNode->ins[i].mixerNode_;
            for (auto& mixerIn : mixer->ins) {
                if (mixerIn.srcPort_ && mixerIn.srcPort_->node_->nodeID >= 0) {
                    connect({mixerIn.srcPort_->node_->nodeID, mixerIn.srcPort_->index_},
                            {newNodeID, i});
                }
            }
        } else {
            OutPort* src = oldNode->ins[i].srcPort_;
            if (src && src->node_->nodeID >= 0) {
                connect({src->node_->nodeID, src->index_}, {newNodeID, i});
            }
        }
    }
    
    // reconnect all outputs
    for (int i = 0; i < oldNode->outs.size(); ++i) {
        reconnectOutput({oldNodeID, i}, {newNodeID, i}, xfadeTime, curve);
    }
    
    return tzpl_errNone;
}

tzpl_SErr disconnectInput(PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;

    InPort* port;
    tzpl_SErr err = tBundle.silo->nrt_getInPort(dst, port);
    if (err != tzpl_errNone) return err;

    Node* xfaderNode = nullptr;
    tzpl_SignalType type = port->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, tBundle.silo, xfadeTime, curve, type);
    }
    tBundle.add(new DisconnectInputCmd(dst, xfaderNode, curve));
    return tzpl_errNone;
}

tzpl_SErr disconnectSource(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;

    OutPort* srcPort;
    tzpl_SErr err = tBundle.silo->nrt_getOutPort(src, srcPort);
    if (err != tzpl_errNone) return err;

    InPort* dstPort;
    err = tBundle.silo->nrt_getInPort(dst, dstPort);
    if (err != tzpl_errNone) return err;

    Node* xfaderNode = nullptr;
    tzpl_SignalType type = dstPort->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, tBundle.silo, xfadeTime, curve, type);
    }
    tBundle.add(new DisconnectSourceCmd(src, dst, xfaderNode, curve));
    return tzpl_errNone;
}

tzpl_SErr disconnectOutput(PortAddr src) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    OutPort* port;
    tzpl_SErr err = tBundle.silo->nrt_getOutPort(src, port);
    if (err != tzpl_errNone) return err;

    tBundle.add(new DisconnectOutputCmd(src));
    return tzpl_errNone;
}

tzpl_SErr disconnectNode(i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    tBundle.add(new DisconnectNodeCmd(nodeID));
    return tzpl_errNone;
}

tzpl_SErr setInput(PortAddr dst, int numValues, void* values, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    InPort* port;
    tzpl_SErr err = tBundle.silo->nrt_getInPort(dst, port);
    if (err != tzpl_errNone) return err;

    tzpl_SignalType type = port->type_;
    switch (type.elem) {
        case tzpl_kI32 :
            tBundle.add(new SetInputCmd<i32>(dst, numValues, (i32*)values, nullptr));
            break;
        case tzpl_kI64 :
            tBundle.add(new SetInputCmd<i64>(dst, numValues, (i64*)values, nullptr));
            break;
        case tzpl_kF32 : {
            Node* xfader = xfadeTime > 0. ? newXFaderNode(e, tBundle.silo, xfadeTime, curve, type) : nullptr;
            tBundle.add(new SetInputCmd<f32>(dst, numValues, (f32*)values, xfader));
        }   break;
        case tzpl_kF64 : {
            Node* xfader = xfadeTime > 0. ? newXFaderNode(e, tBundle.silo, xfadeTime, curve, type) : nullptr;
            tBundle.add(new SetInputCmd<f64>(dst, numValues, (f64*)values, xfader));
        }   break;
    }
    return tzpl_errNone;
}

tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, void* values) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;
    
    // check existence of control.
    Control* c = node->getControl(controlID);
    if (!c) return tzpl_errControlNotFound;

    switch (c->type_.elem) {
        case tzpl_kI32 :
            tBundle.add(new SetControlCmd<i32>(nodeID, controlID, numValues, (i32*)values));
            break;
        case tzpl_kI64 :
            tBundle.add(new SetControlCmd<i64>(nodeID, controlID, numValues, (i64*)values));
            break;
        case tzpl_kF32 : {
            tBundle.add(new SetControlCmd<f32>(nodeID, controlID, numValues, (f32*)values));
        }   break;
        case tzpl_kF64 : {
            tBundle.add(new SetControlCmd<f64>(nodeID, controlID, numValues, (f64*)values));
        }   break;
    }
    return tzpl_errNone;
}

tzpl_SErr allNotesOff(i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    tBundle.add(new AllNotesOffCmd(nodeID));
    return tzpl_errNone;
}

tzpl_SErr noteOn(i64 nodeID, int noteID, int length, f32* paramValues) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    tBundle.add(new NoteOnCmd(nodeID, noteID, length, paramValues));
    return tzpl_errNone;
}

tzpl_SErr noteOff(i64 nodeID, int noteID) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    tBundle.add(new NoteOffCmd(nodeID, noteID));
    return tzpl_errNone;
}

tzpl_SErr noteSetParams(i64 nodeID, int noteID, int n, tzpl_ParamPair* paramValues) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    tBundle.add(new NoteSetParamsCmd(nodeID, noteID, n, paramValues));
    return tzpl_errNone;
}

tzpl_SErr noteSetParamRange(i64 nodeID, int noteID, int first, int length, f32* paramValues) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    tBundle.add(new NoteSetParamRangeCmd(nodeID, noteID, first, length, paramValues));
    return tzpl_errNone;
}

// -- Buffer commands --

tzpl_SErr resizeBuffer(i64 nodeID, i64 bufID, int numChannels, i64 length) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    tBundle.add(new ResizeBufferCmd(nodeID, bufID, numChannels, length));
    return tzpl_errNone;
}

tzpl_SErr loadBuffer(i64 nodeID, i64 bufID, const char* path,
                     int channelOffset, i64 frameOffset, i64 numFrames) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    tBundle.add(new LoadBufferCmd(nodeID, bufID, path, channelOffset, frameOffset, numFrames));
    return tzpl_errNone;
}

tzpl_SErr replaceBuffer(i64 nodeID, i64 bufID, tzpl_Buffer* buffer) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    tBundle.add(new ReplaceBufferCmd(nodeID, bufID, buffer));
    return tzpl_errNone;
}

}
