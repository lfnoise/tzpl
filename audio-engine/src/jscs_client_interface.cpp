//
//  jscs_client_interface.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "jscs_engine.hpp"
#include "jscs_client_interface.hpp"
#include "RtAudio.h"
#include "jscs_hash.hpp"
#include "jscs_command_subclasses.hpp"
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
        f32 const* in = (f32 const*)inputBuffer;
        f32* out = (f32*)outputBuffer;
        
        e->in_ = in;
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


void initAudio(Engine* e) {
    //printf(">initAudio\n");
    auto& rta = e->rtaudio_;
        
    int numDevices = rta->getDeviceCount();
    if (numDevices == 0) {
        throw jscs_errNoAudioDevices;
    }
    
    int deviceID = -1;
    if (strcmp(e->streamParams_.deviceName, "default") == 0) {
        deviceID = rta->getDefaultOutputDevice();
    } else {
        int n = rta->getDeviceCount();
        for (int i = 0; i < n; ++i) {
            auto info = rta->getDeviceInfo(i);
            if (e->streamParams_.deviceName == info.name) {
                deviceID = i;
            }
        }
        if (deviceID < 0) {
            throw jscs_errDeviceNotFound;
        }
    }
            
    RtAudio::StreamParameters outputParams;
    outputParams.deviceId = deviceID;
    outputParams.nChannels = e->streamParams_.channels;
    outputParams.firstChannel = e->streamParams_.firstChannel;

    void* userData = (void*)e;

    {
        rta->setErrorCallback(errorCallback);
        unsigned int bufferFrames = e->streamParams_.bufferFrames;
        rta->openStream(&outputParams, nullptr, RTAUDIO_FLOAT32, e->streamParams_.sampleRate, &bufferFrames, audioCallback,
            userData);

        e->streamParams_.bufferFrames = bufferFrames;
        e->streamParams_.channels = outputParams.nChannels;
    }

    int byteSize = e->streamParams_.bufferFrames * e->streamParams_.channels * sizeof(f32);
    
    //e->safetyLimiter_.reset(new SafetyLimiter(streamParams.bufferFrames, streamParams.channels));
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
    //printf("<initAudio\n");
}

void uninitAudio(Engine* e) {
    stopAudio(e); // in case it was running.

    //std::lock_guard<std::mutex> lck(e->nrt_lock_);

    if (e->audioState_ == AudioState::off) return;

    for (Silo& s : e->silos_) {
        if (s.index_ > 0) {
            free(s.outbuf_);
            s.outbuf_ = nullptr;
        }
    }

    e->safetyLimiter_.reset();

    e->rtaudio_->closeStream();
    e->audioState_ = AudioState::off;
}

void startAudio(Engine* e) {
    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    if (e->audioState_ == AudioState::running) return;
    if (e->audioState_ != AudioState::initted) {
        throw jscs_errAudioNotInitialized;
    }
    e->muteGain_ = 1.f;
    e->rtaudio_->startStream();
    e->audioState_ = AudioState::running;
}

void stopAudio(Engine* e) {
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    if (e->audioState_ != AudioState::running) return;
    e->muteGain_ = 0.f;
    std::this_thread::sleep_for(std::chrono::microseconds(100000));
    e->rtaudio_->stopStream();
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

void addNodeDef(Engine* e, NodeDefInfo const& info) {
    NodeDef* def = new NodeDef(info);
    u32 bin = def->hash_ & kHashMask;

    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    def->next_ = e->defs_[bin];
    e->defs_[bin] = def;
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

jscs_SErr begin(Engine* e, int silo) {
    if (tBundle.head) {
        return jscs_errCommandsQueuedButNotSent;
    }
    if (silo < 0 || silo >= e->silos_.size()) {
        return jscs_errSiloOutOfRange;
    }
    
    tBundle.engine = e;
    tBundle.silo = &e->silos_[silo];
    return jscs_errNone;
}

jscs_SErr sched(f64 time, SchedPolicy policy) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;

    //printf("SCHED %f\n", time);
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    Command* cmds = tBundle.popAll();
    if (!cmds) return jscs_errNone; // not an error, just don't do anything.
    
    Command* cmd = cmds;
    while (cmd) {
        cmd->schedPolicy_ = policy;
        cmd->streamTime_ = time;
        cmd = cmd->next_;
    }
    sendCmds(e, tBundle.silo, cmds);
    tBundle.engine = nullptr;
    return jscs_errNone;
}

jscs_SErr go() { 
    return sched(0., schedImmediate);
}

jscs_SErr newNode(const char* name, i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;

    NodeDef* def = getNodeDef(e, name); // getNodeDef has its own lock.
    if (!def) return jscs_errNodeDefNotFound;

    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    
    Node* test = tBundle.silo->nrt_getNode(nodeID);
    if (test) throw jscs_errNodeIDAlreadyTaken;

    Node* node = nullptr;
    try {
        node = new Node(e, tBundle.silo, def, nodeID);
    } catch (jscs_SErr& err) {
        printf("newNode failed %d\n", err);
        return err;
    }
    
    tBundle.add(new AddNodeCmd{node});
    return jscs_errNone;
}

jscs_SErr freeNode(i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) throw jscs_errNodeNotFound;

    tBundle.add(new RemoveNodeCmd{nodeID});
    return jscs_errNone;
}

jscs_SErr freeAllNodes() {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    tBundle.add(new RemoveAllNodesCmd{});
    return jscs_errNone;
}

// TODO notes, controls, buffers...

jscs_SErr connect(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    jscs_SErr err;
    OutPort* srcPort;
    err = tBundle.silo->nrt_getOutPort(src, srcPort);
    if (err != jscs_errNone) return err;
    
    InPort* dstPort;
    err = tBundle.silo->nrt_getInPort(dst, dstPort);
    if (err != jscs_errNone) return err;

    if (dstPort->node_->nodeID == 0) {
        // destination is the output node. It deals with channel mismatch.
        jscs_SErr err = relaxedCompatibleTypes(srcPort->type_, dstPort->type_);
        if (err != jscs_errNone) return err;
    } else {
        jscs_SErr err = compatibleTypes(srcPort->type_, dstPort->type_);
        if (err != jscs_errNone) return err;
    }

    Node* xfaderNode = nullptr;
    jscs_SignalType type = dstPort->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, tBundle.silo, xfadeTime, curve, type);
    }
    tBundle.add(new ConnectCmd(src, dst, xfaderNode, curve));
    return jscs_errNone;
}

jscs_SErr reconnectOutput(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    jscs_SErr err;
    OutPort* oldSrcPort;
    err = tBundle.silo->nrt_getOutPort(oldSrc, oldSrcPort);
    if (err != jscs_errNone) return err;
    
    OutPort* newSrcPort;
    err = tBundle.silo->nrt_getOutPort(newSrc, newSrcPort);
    if (err != jscs_errNone) return err;

    err = compatibleTypes(oldSrcPort->type_, newSrcPort->type_);
    if (err != jscs_errNone) return err;

    Node* xfaderNode = nullptr;
    jscs_SignalType type = newSrcPort->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, tBundle.silo, xfadeTime, curve, type);
    }
    tBundle.add(new ReconnectOutputCmd(oldSrc, newSrc, xfaderNode, curve));
    return jscs_errNone;
}

jscs_SErr replaceNode(i64 oldNodeID, i64 newNodeID, f64 xfadeTime, FadeCurve curve)
{
    Silo* s = tBundle.silo;
    Node* oldNode = s->nrt_getNode(oldNodeID);
    Node* newNode = s->nrt_getNode(newNodeID);
    
    if (oldNode->ins.size() != newNode->ins.size()) return jscs_errNumPortsMismatch;
    if (oldNode->outs.size() != newNode->outs.size()) return jscs_errNumPortsMismatch;
    
    for (int i = 0; i < oldNode->ins.size(); ++i) {
        jscs_SErr err = compatibleTypes(oldNode->ins[i].type_, newNode->ins[i].type_);
        if (err) return err;
    }
    for (int i = 0; i < oldNode->outs.size(); ++i) {
        jscs_SErr err = compatibleTypes(oldNode->outs[i].type_, newNode->outs[i].type_);
        if (err) return err;
    }
    
    // connect new node to all the same inputs.
    for (int i = 0; i < oldNode->ins.size(); ++i) {
        OutPort* src = oldNode->ins[i].srcPort_;
        i64 srcNodeID = src->node_->nodeID;
        connect({srcNodeID, src->index_}, {newNodeID, i});
    }
    
    // reconnect all outputs
    for (int i = 0; i < oldNode->outs.size(); ++i) {
        reconnectOutput({oldNodeID, i}, {newNodeID, i}, xfadeTime, curve);
    }
    
    return jscs_errNone;
}

jscs_SErr disconnectInput(PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) throw jscs_errNoActiveBundle;
    
    InPort* port;
    jscs_SErr err = tBundle.silo->nrt_getInPort(dst, port);
    if (err != jscs_errNone) return err;

    Node* xfaderNode = nullptr;
    jscs_SignalType type = port->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, tBundle.silo, xfadeTime, curve, type);
    }
    tBundle.add(new DisconnectInputCmd(dst, xfaderNode, curve));
    return jscs_errNone;
}

jscs_SErr disconnectOutput(PortAddr src) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    OutPort* port;
    jscs_SErr err = tBundle.silo->nrt_getOutPort(src, port);
    if (err != jscs_errNone) return err;

    tBundle.add(new DisconnectOutputCmd(src));
    return jscs_errNone;
}

jscs_SErr disconnectNode(i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return jscs_errNodeNotFound;

    tBundle.add(new DisconnectNodeCmd(nodeID));
    return jscs_errNone;
}

jscs_SErr setInput(PortAddr dst, int numValues, void* values, f64 xfadeTime, FadeCurve curve) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    InPort* port;
    jscs_SErr err = tBundle.silo->nrt_getInPort(dst, port);
    if (err != jscs_errNone) return err;

    jscs_SignalType type = port->type_;
    switch (type.elem) {
        case jscs_kI32 :
            tBundle.add(new SetInputCmd<i32>(dst, numValues, (i32*)values, nullptr));
            break;
        case jscs_kI64 :
            tBundle.add(new SetInputCmd<i64>(dst, numValues, (i64*)values, nullptr));
            break;
        case jscs_kF32 : {
            Node* xfader = xfadeTime > 0. ? newXFaderNode(e, tBundle.silo, xfadeTime, curve, type) : nullptr;
            tBundle.add(new SetInputCmd<f32>(dst, numValues, (f32*)values, xfader));
        }   break;
        case jscs_kF64 : {
            Node* xfader = xfadeTime > 0. ? newXFaderNode(e, tBundle.silo, xfadeTime, curve, type) : nullptr;
            tBundle.add(new SetInputCmd<f64>(dst, numValues, (f64*)values, xfader));
        }   break;
    }
    return jscs_errNone;
}

jscs_SErr setControl(i64 nodeID, i64 controlID, int numValues, void* values) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return jscs_errNodeNotFound;
    
    // check existence of control.
    Control* c = node->getControl(controlID);
    if (!c) return jscs_errControlNotFound;

    switch (c->type_.elem) {
        case jscs_kI32 :
            tBundle.add(new SetControlCmd<i32>(nodeID, controlID, numValues, (i32*)values));
            break;
        case jscs_kI64 :
            tBundle.add(new SetControlCmd<i64>(nodeID, controlID, numValues, (i64*)values));
            break;
        case jscs_kF32 : {
            tBundle.add(new SetControlCmd<f32>(nodeID, controlID, numValues, (f32*)values));
        }   break;
        case jscs_kF64 : {
            tBundle.add(new SetControlCmd<f64>(nodeID, controlID, numValues, (f64*)values));
        }   break;
    }
    return jscs_errNone;
}

jscs_SErr allNotesOff(i64 nodeID) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return jscs_errNodeNotFound;

    tBundle.add(new AllNotesOffCmd(nodeID));
    return jscs_errNone;
}

jscs_SErr noteOn(i64 nodeID, int noteID, int length, f32* paramValues) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return jscs_errNodeNotFound;

    tBundle.add(new NoteOnCmd(nodeID, noteID, length, paramValues));
    return jscs_errNone;
}

jscs_SErr noteOff(i64 nodeID, int noteID) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return jscs_errNodeNotFound;

    tBundle.add(new NoteOffCmd(nodeID, noteID));
    return jscs_errNone;
}

jscs_SErr noteSetParams(i64 nodeID, int noteID, int n, jscs_ParamPair* paramValues) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return jscs_errNodeNotFound;

    tBundle.add(new NoteSetParamsCmd(nodeID, noteID, n, paramValues));
    return jscs_errNone;
}

jscs_SErr noteSetParamRange(i64 nodeID, int noteID, int first, int length, f32* paramValues) {
    Engine* e = tBundle.engine;
    if (!e) return jscs_errNoActiveBundle;
    
    Node* node = tBundle.silo->nrt_getNode(nodeID);
    if (!node) return jscs_errNodeNotFound;

    tBundle.add(new NoteSetParamRangeCmd(nodeID, noteID, first, length, paramValues));
    return jscs_errNone;
}

}
