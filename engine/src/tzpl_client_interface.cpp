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
#include "tzpl_audio_backend_rtaudio.hpp"
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

Engine* newEngine(EngineConfig const& config, AudioStreamParameters& asp,
                  std::unique_ptr<AudioBackend> backend) {
#if DEBUG_NODES
    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    numNodesCreated = 0;
    numNodesDeleted = 0;
#endif
    if (!backend) backend = std::make_unique<RtAudioBackend>();
    return new Engine(config, asp, std::move(backend));
}

Engine* newEngineNRT(EngineConfig const& config, AudioStreamParameters& asp) {
#if DEBUG_NODES
    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    numNodesCreated = 0;
    numNodesDeleted = 0;
#endif
    // NRT mode never uses input.
    asp.inputChannels = 0;
    asp.firstInputChannel = 0;
    asp.inputDeviceName = nullptr;
    return new Engine(config, asp, /*nrt=*/true);
}

void copyNodeDefs(Engine* from, Engine* to) {
    std::lock_guard<std::mutex> lck(from->nrt_lock_);
    for (u32 bin = 0; bin < kHashBins; ++bin) {
        NodeDef* def = from->defs_[bin];
        while (def) {
            if (!def->superseded_
                && strcmp(def->info_.name, "Audio Out") != 0
                && strcmp(def->info_.name, "Audio In") != 0) {
                // Retain the dylib by bumping its dlopen refcount so it
                // stays loaded even if the source engine hot-reloads (and
                // dlcloses) the original during concurrent evaluation.
                void* handle = nullptr;
                if (def->dlHandle_) {
                    Dl_info dl;
                    if (dladdr((void*)def->info_.funs.processAudio, &dl)
                        && dl.dli_fname) {
                        handle = dlopen(dl.dli_fname, RTLD_NOW);
                    }
                }
                addNodeDef(to, def->info_, handle);
            }
            def = def->next_;
        }
    }
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

void masterGain(Engine* e, f32 gain) {
    e->masterGain_ = gain;
}

void safetyLimiter(Engine* e, Enable onoff) {
    e->enableSafetyLimiter_ = onoff;
}

// Shared dispatch: signals worker silos, runs silo 0 on the calling thread,
// applies the safety limiter, and advances anchorSampleTime_. Used by both
// the backend RT callbacks and the NRT renderer.
void processAudioBlock(Engine* e, f32 const* in, f32* out,
                       unsigned int numFrames, f64 streamTime) {
    e->in_ = in;
    e->out_ = out;
    e->anchorStreamTime_ = streamTime;

    // Release worker silos.
    int numSilos = int(e->silos_.size());
    for (int i = 1; i < numSilos; ++i) {
        e->silos_[i].start_sem_.signal();
    }

    int numSamples = numFrames * e->streamParams_.channels;
    memset(out, 0, sizeof(f32) * numSamples);

    e->silos_[0].processFrames(); // silo 0 runs on the calling (audio/render) thread.

    f32 gain = e->masterGain_ * e->muteGain_;
    e->safetyLimiter_->process(out, e->enableSafetyLimiter_, gain);
    e->anchorSampleTime_ += numFrames;
}

void renderNRTBlock(Engine* e, f32* outBuffer) {
    unsigned int numFrames = e->streamParams_.bufferFrames;
    f64 streamTime = (f64)e->anchorSampleTime_ / e->streamParams_.sampleRate;
    processAudioBlock(e, /*in=*/nullptr, outBuffer, numFrames, streamTime);
    e->drainNRTQueues();
}

void initAudio(Engine* e) {
    // Open the device(s). The backend may adjust streamParams_ (bufferFrames,
    // channels, inputChannels) to what the device negotiated, so the silo and
    // limiter buffers below must be sized after this call.
    e->backend_->init(e);

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

    if (e->nrtMode_) {
        e->audioState_ = AudioState::off;
        return;
    }

    e->backend_->uninit();
    e->audioState_ = AudioState::off;
}

void startAudio(Engine* e) {
    if (e->nrtMode_) return; // NRT mode: renderer drives processing.
    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    if (e->audioState_ == AudioState::running) return;
    if (e->audioState_ != AudioState::initted) {
        throw tzpl_errAudioNotInitialized;
    }
    e->muteGain_ = 1.f;
    e->backend_->start();
    e->audioState_ = AudioState::running;
}

void stopAudio(Engine* e) {
    if (e->nrtMode_) return;
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    if (e->audioState_ != AudioState::running) return;
    e->muteGain_ = 0.f;
    std::this_thread::sleep_for(std::chrono::microseconds(100000));
    e->backend_->stop();
    e->audioState_ = AudioState::initted;
}

void printDevices(Engine* e)
{
    if (!e->backend_) return; // NRT engines have no device backend.
    e->backend_->printDevices();
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

bool listDefControls(Engine* e, const char* defName, std::vector<ControlDesc>& out) {
    u64 hash = hash64(defName, kHashStart);
    u32 bin = hash & kHashMask;

    // Copy while holding the lock so the def can't be released underneath us.
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    NodeDef* def = e->defs_[bin];
    while (def) {
        if (def->hash_ == hash && strcmp(def->info_.name, defName) == 0) break;
        def = def->next_;
    }
    if (!def) return false;
    for (int i = 0; i < def->info_.num_controls; ++i) {
        ControlInfo const& c = def->info_.controls[i];
        out.push_back({c.name ? c.name : "", c.controlID, c.spec});
    }
    return true;
}

// ============================================================================
// Command bundling.
//
// The command builders (newNode, connect, setInput, ...) do not touch any
// silo when called: each call records a BundleOp into the thread-local
// bundle. The target silo is chosen at submit time -- go(silo) /
// sched(silo, clock, beat) -- which replays the recorded ops in order
// against that silo, validating and materializing the actual Commands.
// On the first error the ENTIRE bundle is discarded (atomic abort): nothing
// is applied, and everything pre-allocated during replay is freed via the
// stage_ == 0 guards in the Command destructors. Submit always closes the
// bundle, success or failure.
// ============================================================================

struct BundleOp
{
    BundleOp* next_ = nullptr;
    virtual ~BundleOp() = default;
    // Validate against the chosen silo and append the materialized
    // Command(s) to `out`. Runs on the submitting (NRT) thread.
    virtual tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) = 0;
};

struct CmdBundle
{
    Engine* engine = nullptr;
    BundleOp* head = nullptr;
    BundleOp* tail = nullptr;

    void add(BundleOp* op) noexcept {
        op->next_ = nullptr;
        if (tail) tail->next_ = op;
        else head = op;
        tail = op;
    }

    void reset() noexcept {
        BundleOp* op = head;
        while (op) {
            BundleOp* next = op->next_;
            delete op;
            op = next;
        }
        head = tail = nullptr;
        engine = nullptr;
    }
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

// ----------------------------------------------------------------------------
// Apply helpers: the silo-dependent halves of the command builders. Each one
// validates against the chosen silo and appends the materialized Command(s)
// to `out`. These replay in recorded order, so an op that references a node
// created earlier in the same bundle resolves it via the NRT mirror exactly
// as the eager builders used to.
// ----------------------------------------------------------------------------

static tzpl_SErr applyNewNode(Engine* e, Silo* s, CommandList& out,
                              char const* name, i64 nodeID) {
    NodeDef* def = getNodeDef(e, name); // getNodeDef has its own lock.
    if (!def) return tzpl_errNodeDefNotFound;

    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    Node* test = s->nrt_getNode(nodeID);
    if (test) return tzpl_errNodeIDAlreadyTaken;

    Node* node = nullptr;
    try {
        node = new Node(e, s, def, nodeID);
    } catch (tzpl_SErr& err) {
        printf("newNode failed %d\n", err);
        return err;
    }

    out.add(new AddNodeCmd{node});
    return tzpl_errNone;
}

static tzpl_SErr applyConnect(Engine* e, Silo* s, CommandList& out,
                              PortAddr src, PortAddr dst,
                              f64 xfadeTime, FadeCurve curve) {
    tzpl_SErr err;
    OutPort* srcPort;
    err = s->nrt_getOutPort(src, srcPort);
    if (err != tzpl_errNone) return err;

    InPort* dstPort;
    err = s->nrt_getInPort(dst, dstPort);
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
    Node* mixerNode = newMixerNode(e, s, type, 4);

    Node* xfaderNode = nullptr;
    if (xfadeTime > 0. && isFloat(srcPort->type_.elem)) {
        xfaderNode = newXFaderNode(e, s, xfadeTime, curve, srcPort->type_);
    }
    out.add(new ConnectCmd(src, dst, xfaderNode, curve, mixerNode, -1));
    return tzpl_errNone;
}

static tzpl_SErr applyReconnectOutput(Engine* e, Silo* s, CommandList& out,
                                      PortAddr oldSrc, PortAddr newSrc,
                                      f64 xfadeTime, FadeCurve curve) {
    tzpl_SErr err;
    OutPort* oldSrcPort;
    err = s->nrt_getOutPort(oldSrc, oldSrcPort);
    if (err != tzpl_errNone) return err;

    OutPort* newSrcPort;
    err = s->nrt_getOutPort(newSrc, newSrcPort);
    if (err != tzpl_errNone) return err;

    err = compatibleTypes(oldSrcPort->type_, newSrcPort->type_);
    if (err != tzpl_errNone) return err;

    Node* xfaderNode = nullptr;
    tzpl_SignalType type = newSrcPort->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, s, xfadeTime, curve, type);
    }
    out.add(new ReconnectOutputCmd(oldSrc, newSrc, xfaderNode, curve));
    return tzpl_errNone;
}

static tzpl_SErr applyReplaceNode(Engine* e, Silo* s, CommandList& out,
                                  i64 oldNodeID, i64 newNodeID,
                                  f64 xfadeTime, FadeCurve curve) {
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
    // and connect each one. The additive connect will automatically
    // create a new mixer on the new node's input.
    // NB: call the apply helpers directly (NOT the public builders, which
    // would append ops to the bundle being replayed).
    for (int i = 0; i < (int)oldNode->ins.size(); ++i) {
        if (oldNode->ins[i].mixerNode_) {
            Node* mixer = oldNode->ins[i].mixerNode_;
            for (auto& mixerIn : mixer->ins) {
                if (mixerIn.srcPort_ && mixerIn.srcPort_->node_->nodeID >= 0) {
                    applyConnect(e, s, out,
                            {mixerIn.srcPort_->node_->nodeID, mixerIn.srcPort_->index_},
                            {newNodeID, i}, 0., fadeLinear);
                }
            }
        } else {
            OutPort* src = oldNode->ins[i].srcPort_;
            if (src && src->node_->nodeID >= 0) {
                applyConnect(e, s, out,
                        {src->node_->nodeID, src->index_}, {newNodeID, i},
                        0., fadeLinear);
            }
        }
    }

    // reconnect all outputs
    for (int i = 0; i < oldNode->outs.size(); ++i) {
        applyReconnectOutput(e, s, out, {oldNodeID, i}, {newNodeID, i}, xfadeTime, curve);
    }

    return tzpl_errNone;
}

static tzpl_SErr applyDisconnectInput(Engine* e, Silo* s, CommandList& out,
                                      PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    InPort* port;
    tzpl_SErr err = s->nrt_getInPort(dst, port);
    if (err != tzpl_errNone) return err;

    Node* xfaderNode = nullptr;
    tzpl_SignalType type = port->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, s, xfadeTime, curve, type);
    }
    out.add(new DisconnectInputCmd(dst, xfaderNode, curve));
    return tzpl_errNone;
}

static tzpl_SErr applyDisconnectSource(Engine* e, Silo* s, CommandList& out,
                                       PortAddr src, PortAddr dst,
                                       f64 xfadeTime, FadeCurve curve) {
    OutPort* srcPort;
    tzpl_SErr err = s->nrt_getOutPort(src, srcPort);
    if (err != tzpl_errNone) return err;

    InPort* dstPort;
    err = s->nrt_getInPort(dst, dstPort);
    if (err != tzpl_errNone) return err;

    Node* xfaderNode = nullptr;
    tzpl_SignalType type = dstPort->type_;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, s, xfadeTime, curve, type);
    }
    out.add(new DisconnectSourceCmd(src, dst, xfaderNode, curve));
    return tzpl_errNone;
}

static tzpl_SErr applyDisconnectOutput(Engine*, Silo* s, CommandList& out, PortAddr src) {
    OutPort* port;
    tzpl_SErr err = s->nrt_getOutPort(src, port);
    if (err != tzpl_errNone) return err;

    out.add(new DisconnectOutputCmd(src));
    return tzpl_errNone;
}

// Convert the values captured at record time (srcElem-typed bytes) into the
// destination element type. When the types match this is a straight copy;
// when they differ, convert per element (the old void* API silently
// reinterpreted the buffer in that case).
template <class T>
static std::vector<T> convertValues(tzpl_ElemType srcElem, void const* bytes, int n) {
    std::vector<T> out(n);
    switch (srcElem) {
        case tzpl_kI32 : {
            i32 const* p = static_cast<i32 const*>(bytes);
            for (int i = 0; i < n; ++i) out[i] = static_cast<T>(p[i]);
        } break;
        case tzpl_kI64 : {
            i64 const* p = static_cast<i64 const*>(bytes);
            for (int i = 0; i < n; ++i) out[i] = static_cast<T>(p[i]);
        } break;
        case tzpl_kF32 : {
            f32 const* p = static_cast<f32 const*>(bytes);
            for (int i = 0; i < n; ++i) out[i] = static_cast<T>(p[i]);
        } break;
        case tzpl_kF64 : {
            f64 const* p = static_cast<f64 const*>(bytes);
            for (int i = 0; i < n; ++i) out[i] = static_cast<T>(p[i]);
        } break;
    }
    return out;
}

static tzpl_SErr applySetInput(Engine* e, Silo* s, CommandList& out,
                               PortAddr dst, tzpl_ElemType srcElem,
                               void const* bytes, int numValues,
                               f64 xfadeTime, FadeCurve curve) {
    InPort* port;
    tzpl_SErr err = s->nrt_getInPort(dst, port);
    if (err != tzpl_errNone) return err;

    tzpl_SignalType type = port->type_;
    switch (type.elem) {
        case tzpl_kI32 : {
            auto v = convertValues<i32>(srcElem, bytes, numValues);
            out.add(new SetInputCmd<i32>(dst, numValues, v.data(), nullptr));
        }   break;
        case tzpl_kI64 : {
            auto v = convertValues<i64>(srcElem, bytes, numValues);
            out.add(new SetInputCmd<i64>(dst, numValues, v.data(), nullptr));
        }   break;
        case tzpl_kF32 : {
            auto v = convertValues<f32>(srcElem, bytes, numValues);
            Node* xfader = xfadeTime > 0. ? newXFaderNode(e, s, xfadeTime, curve, type) : nullptr;
            out.add(new SetInputCmd<f32>(dst, numValues, v.data(), xfader));
        }   break;
        case tzpl_kF64 : {
            auto v = convertValues<f64>(srcElem, bytes, numValues);
            Node* xfader = xfadeTime > 0. ? newXFaderNode(e, s, xfadeTime, curve, type) : nullptr;
            out.add(new SetInputCmd<f64>(dst, numValues, v.data(), xfader));
        }   break;
    }
    return tzpl_errNone;
}

static tzpl_SErr applyTapOutlet(Engine* e, Silo* s, CommandList& out,
                                i64 nodeID, int outlet, i64 tapID, int mode) {
    OutPort* port;
    tzpl_SErr err = s->nrt_getOutPort(PortAddr{nodeID, outlet}, port);
    if (err != tzpl_errNone) return err;
    if (port->type_.elem != tzpl_kF32) return tzpl_errTypeMismatch;
    if (mode != tapMeter && mode != tapScope) return tzpl_errNotImplemented;

    TapSlot* slot;
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        auto [it, inserted] = e->taps_.try_emplace(
            tapID, std::make_unique<TapSlot>(static_cast<TapMode>(mode)));
        if (!inserted) return tzpl_errAlreadyAdded;
        slot = it->second.get();
        slot->chans = std::min(port->type_.chans,
                               (decltype(port->type_.chans))TapSlot::kMaxScopeChans);
        if (slot->chans < 1) slot->chans = 1;
    }
    out.add(new TapOutletCmd(e, nodeID, outlet, tapID, slot));
    return tzpl_errNone;
}

static tzpl_SErr applyUntap(Engine* e, Silo*, CommandList& out, i64 tapID) {
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        if (!e->taps_.count(tapID)) return tzpl_errNodeNotFound;
    }
    out.add(new UntapCmd(tapID));
    return tzpl_errNone;
}

static tzpl_SErr applySetControl(Engine*, Silo* s, CommandList& out,
                                 i64 nodeID, i64 controlID,
                                 tzpl_ElemType srcElem, void const* bytes, int numValues) {
    Node* node = s->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    // check existence of control.
    Control* c = node->getControl(controlID);
    if (!c) return tzpl_errControlNotFound;

    switch (c->type_.elem) {
        case tzpl_kI32 : {
            auto v = convertValues<i32>(srcElem, bytes, numValues);
            out.add(new SetControlCmd<i32>(nodeID, controlID, numValues, v.data()));
        }   break;
        case tzpl_kI64 : {
            auto v = convertValues<i64>(srcElem, bytes, numValues);
            out.add(new SetControlCmd<i64>(nodeID, controlID, numValues, v.data()));
        }   break;
        case tzpl_kF32 : {
            auto v = convertValues<f32>(srcElem, bytes, numValues);
            out.add(new SetControlCmd<f32>(nodeID, controlID, numValues, v.data()));
        }   break;
        case tzpl_kF64 : {
            auto v = convertValues<f64>(srcElem, bytes, numValues);
            out.add(new SetControlCmd<f64>(nodeID, controlID, numValues, v.data()));
        }   break;
    }
    return tzpl_errNone;
}

// ----------------------------------------------------------------------------
// Bundle op records. All argument data is OWNED by the op (copied at record
// time): the caller's strings/arrays need not outlive the builder call.
// ----------------------------------------------------------------------------

// A command that was fully built at record time and needs no silo-side
// validation (freeAllNodes, channelOffset, buffer commands, sendCommand).
struct PrebuiltCmdOp : BundleOp
{
    Command* cmd_;

    explicit PrebuiltCmdOp(Command* cmd) : cmd_(cmd) {}
    ~PrebuiltCmdOp() override { delete cmd_; } // owned until applied

    tzpl_SErr apply(Engine*, Silo*, CommandList& out) override {
        out.add(cmd_);
        cmd_ = nullptr;
        return tzpl_errNone;
    }
};

// A command that was fully built at record time and whose only silo-side
// validation is that the target node exists (freeNode, disconnectNode, and
// the note/voice commands).
struct NodeCheckedCmdOp : BundleOp
{
    i64 nodeID_;
    Command* cmd_;

    NodeCheckedCmdOp(i64 nodeID, Command* cmd) : nodeID_(nodeID), cmd_(cmd) {}
    ~NodeCheckedCmdOp() override { delete cmd_; } // owned until applied

    tzpl_SErr apply(Engine*, Silo* s, CommandList& out) override {
        if (!s->nrt_getNode(nodeID_)) return tzpl_errNodeNotFound;
        out.add(cmd_);
        cmd_ = nullptr;
        return tzpl_errNone;
    }
};

struct NewNodeOp : BundleOp
{
    std::string defName_;
    i64 nodeID_;

    NewNodeOp(char const* defName, i64 nodeID) : defName_(defName), nodeID_(nodeID) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyNewNode(e, s, out, defName_.c_str(), nodeID_);
    }
};

struct ConnectOp : BundleOp
{
    PortAddr src_, dst_;
    f64 xfadeTime_;
    FadeCurve curve_;

    ConnectOp(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve)
        : src_(src), dst_(dst), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyConnect(e, s, out, src_, dst_, xfadeTime_, curve_);
    }
};

struct ReconnectOutputOp : BundleOp
{
    PortAddr oldSrc_, newSrc_;
    f64 xfadeTime_;
    FadeCurve curve_;

    ReconnectOutputOp(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime, FadeCurve curve)
        : oldSrc_(oldSrc), newSrc_(newSrc), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyReconnectOutput(e, s, out, oldSrc_, newSrc_, xfadeTime_, curve_);
    }
};

struct ReplaceNodeOp : BundleOp
{
    i64 oldNodeID_, newNodeID_;
    f64 xfadeTime_;
    FadeCurve curve_;

    ReplaceNodeOp(i64 oldNodeID, i64 newNodeID, f64 xfadeTime, FadeCurve curve)
        : oldNodeID_(oldNodeID), newNodeID_(newNodeID), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyReplaceNode(e, s, out, oldNodeID_, newNodeID_, xfadeTime_, curve_);
    }
};

struct DisconnectInputOp : BundleOp
{
    PortAddr dst_;
    f64 xfadeTime_;
    FadeCurve curve_;

    DisconnectInputOp(PortAddr dst, f64 xfadeTime, FadeCurve curve)
        : dst_(dst), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyDisconnectInput(e, s, out, dst_, xfadeTime_, curve_);
    }
};

struct DisconnectSourceOp : BundleOp
{
    PortAddr src_, dst_;
    f64 xfadeTime_;
    FadeCurve curve_;

    DisconnectSourceOp(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve)
        : src_(src), dst_(dst), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyDisconnectSource(e, s, out, src_, dst_, xfadeTime_, curve_);
    }
};

struct DisconnectOutputOp : BundleOp
{
    PortAddr src_;

    explicit DisconnectOutputOp(PortAddr src) : src_(src) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyDisconnectOutput(e, s, out, src_);
    }
};

struct SetInputOp : BundleOp
{
    PortAddr dst_;
    tzpl_ElemType elem_;
    int numValues_;
    f64 xfadeTime_;
    FadeCurve curve_;
    std::vector<u8> bytes_;

    SetInputOp(PortAddr dst, tzpl_ElemType elem, int numValues, void const* values,
               f64 xfadeTime, FadeCurve curve)
        : dst_(dst), elem_(elem), numValues_(numValues),
          xfadeTime_(xfadeTime), curve_(curve),
          bytes_(numValues * elemSize(elem))
    {
        memcpy(bytes_.data(), values, bytes_.size());
    }

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applySetInput(e, s, out, dst_, elem_, bytes_.data(), numValues_,
                             xfadeTime_, curve_);
    }
};

struct SetControlOp : BundleOp
{
    i64 nodeID_, controlID_;
    tzpl_ElemType elem_;
    int numValues_;
    std::vector<u8> bytes_;

    SetControlOp(i64 nodeID, i64 controlID, tzpl_ElemType elem, int numValues,
                 void const* values)
        : nodeID_(nodeID), controlID_(controlID), elem_(elem), numValues_(numValues),
          bytes_(numValues * elemSize(elem))
    {
        memcpy(bytes_.data(), values, bytes_.size());
    }

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applySetControl(e, s, out, nodeID_, controlID_, elem_,
                               bytes_.data(), numValues_);
    }
};

struct TapOutletOp : BundleOp
{
    i64 nodeID_;
    int outlet_;
    i64 tapID_;
    int mode_;

    TapOutletOp(i64 nodeID, int outlet, i64 tapID, int mode)
        : nodeID_(nodeID), outlet_(outlet), tapID_(tapID), mode_(mode)
    {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyTapOutlet(e, s, out, nodeID_, outlet_, tapID_, mode_);
    }
};

struct UntapOp : BundleOp
{
    i64 tapID_;

    UntapOp(i64 tapID) : tapID_(tapID) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out) override {
        return applyUntap(e, s, out, tapID_);
    }
};

// ----------------------------------------------------------------------------
// Bundle open / submit.
// ----------------------------------------------------------------------------

void sendCommand(Command* cmd) {
    tBundle.add(new PrebuiltCmdOp(cmd));
}

tzpl_SErr begin(Engine* e) {
    if (tBundle.head) {
        return tzpl_errCommandsQueuedButNotSent;
    }
    if (!e) return tzpl_errInternal;

    tBundle.engine = e;
    return tzpl_errNone;
}

tzpl_SErr sched(int silo, int clock, f64 beat, SchedPolicy policy) {
    Engine* e = tBundle.engine;
    if (!e) return tzpl_errNoActiveBundle;
    if (silo < 0 || silo >= (int)e->silos_.size()) {
        tBundle.reset();
        return tzpl_errSiloOutOfRange;
    }
    if (policy != schedImmediate &&
        (clock < 0 || clock >= e->numTempoClocks_)) {
        tBundle.reset();
        return tzpl_errClockOutOfRange;
    }
    Silo* s = &e->silos_[silo];

    // Materialize the recorded ops against the chosen silo, in order.
    // On the first error the whole bundle is discarded: deleting the
    // never-run commands (stage_ == 0) frees everything they pre-allocated,
    // including nodes already inserted into the silo's NRT table.
    CommandList cmds;
    for (BundleOp* op = tBundle.head; op; op = op->next_) {
        tzpl_SErr err = op->apply(e, s, cmds);
        if (err != tzpl_errNone) {
            {
                // ~Node runs releaseNodeDef and unlinks the NRT table,
                // both of which require nrt_lock_.
                std::lock_guard<std::mutex> lck(e->nrt_lock_);
                cmds.clear();
            }
            tBundle.reset();
            return err;
        }
    }
    tBundle.reset();

    Command* head = cmds.popAll();
    if (!head) return tzpl_errNone; // empty bundle: closed, nothing to do.

    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    for (Command* cmd = head; cmd; cmd = cmd->next_) {
        cmd->schedPolicy_ = policy;
        cmd->clock_ = clock;
        cmd->beatTime_ = beat;
    }
    sendCmds(e, s, head);
    return tzpl_errNone;
}

tzpl_SErr go(int silo) {
    return sched(silo, 0, 0., schedImmediate);
}

// Broadcast a tempo command (one instance per silo) to keep clock slot `clock`
// in sync across all silos. makeCmd allocates a fresh Command for each silo.
template <typename MakeCmd>
static tzpl_SErr broadcastTempoCmd(Engine* e, int clock, MakeCmd makeCmd) {
    if (!e) return tzpl_errNoActiveBundle;
    if (clock < 0 || clock >= e->numTempoClocks_) return tzpl_errClockOutOfRange;

    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    for (Silo& s : e->silos_) {
        sendCmds(e, &s, makeCmd());
    }
    return tzpl_errNone;
}

tzpl_SErr setTempo(Engine* e, int clock, f64 bpm) {
    return broadcastTempoCmd(e, clock, [=] {
        return new SetClockTempoCmd(clock, bpm / 60.0); // BPM -> beats per second
    });
}

tzpl_SErr schedTempoChange(Engine* e, int clock, f64 atBeat, f64 targetBPM, f64 rampBeats) {
    return broadcastTempoCmd(e, clock, [=] {
        return new SchedClockTempoRampCmd(clock, atBeat, targetBPM / 60.0, rampBeats);
    });
}

f64 clockBeats(Engine* e, int clock) {
    if (!e || clock < 0 || clock >= e->numTempoClocks_) return 0.;
    Silo& s = e->silos_[0];
    if (clock >= (int)s.tempoClocks_.size()) return 0.;
    return s.tempoClocks_[clock].beatAtSample(s.sampleTime_);
}

f64 clockTempoBPM(Engine* e, int clock) {
    if (!e || clock < 0 || clock >= e->numTempoClocks_) return 0.;
    Silo& s = e->silos_[0];
    if (clock >= (int)s.tempoClocks_.size()) return 0.;
    return s.tempoClocks_[clock].tempoAtSample(s.sampleTime_) * 60.0; // BPS -> BPM
}

// ----------------------------------------------------------------------------
// Command builders. Each call records an op into the thread-local bundle;
// all silo-side validation happens when the bundle is submitted with
// go(silo) / sched(silo, ...). The only error a builder itself can return
// is tzpl_errNoActiveBundle.
// ----------------------------------------------------------------------------

tzpl_SErr newNode(const char* name, i64 nodeID) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new NewNodeOp(name, nodeID));
    return tzpl_errNone;
}

tzpl_SErr freeNode(i64 nodeID) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new NodeCheckedCmdOp(nodeID, new RemoveNodeCmd{nodeID}));
    return tzpl_errNone;
}

tzpl_SErr freeAllNodes() {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new PrebuiltCmdOp(new RemoveAllNodesCmd{}));
    return tzpl_errNone;
}

tzpl_SErr channelOffset(i32 offset) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new PrebuiltCmdOp(new ChannelOffsetCmd{offset}));
    return tzpl_errNone;
}

tzpl_SErr connect(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new ConnectOp(src, dst, xfadeTime, curve));
    return tzpl_errNone;
}

tzpl_SErr reconnectOutput(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime, FadeCurve curve) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new ReconnectOutputOp(oldSrc, newSrc, xfadeTime, curve));
    return tzpl_errNone;
}

tzpl_SErr replaceNode(i64 oldNodeID, i64 newNodeID, f64 xfadeTime, FadeCurve curve) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new ReplaceNodeOp(oldNodeID, newNodeID, xfadeTime, curve));
    return tzpl_errNone;
}

tzpl_SErr disconnectInput(PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new DisconnectInputOp(dst, xfadeTime, curve));
    return tzpl_errNone;
}

tzpl_SErr disconnectSource(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new DisconnectSourceOp(src, dst, xfadeTime, curve));
    return tzpl_errNone;
}

tzpl_SErr disconnectOutput(PortAddr src) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new DisconnectOutputOp(src));
    return tzpl_errNone;
}

tzpl_SErr disconnectNode(i64 nodeID) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new NodeCheckedCmdOp(nodeID, new DisconnectNodeCmd(nodeID)));
    return tzpl_errNone;
}

// setInput / setControl capture the caller's values with their static type;
// at submit the values are converted to the port's / control's element type
// if they differ.

static tzpl_SErr recordSetInput(PortAddr dst, tzpl_ElemType elem, int numValues,
                                void const* values, f64 xfadeTime, FadeCurve curve) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new SetInputOp(dst, elem, numValues, values, xfadeTime, curve));
    return tzpl_errNone;
}

tzpl_SErr setInput(PortAddr dst, int numValues, f32 const* values, f64 xfadeTime, FadeCurve curve) {
    return recordSetInput(dst, tzpl_kF32, numValues, values, xfadeTime, curve);
}
tzpl_SErr setInput(PortAddr dst, int numValues, f64 const* values, f64 xfadeTime, FadeCurve curve) {
    return recordSetInput(dst, tzpl_kF64, numValues, values, xfadeTime, curve);
}
tzpl_SErr setInput(PortAddr dst, int numValues, i32 const* values, f64 xfadeTime, FadeCurve curve) {
    return recordSetInput(dst, tzpl_kI32, numValues, values, xfadeTime, curve);
}
tzpl_SErr setInput(PortAddr dst, int numValues, i64 const* values, f64 xfadeTime, FadeCurve curve) {
    return recordSetInput(dst, tzpl_kI64, numValues, values, xfadeTime, curve);
}

static tzpl_SErr recordSetControl(i64 nodeID, i64 controlID, tzpl_ElemType elem,
                                  int numValues, void const* values) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new SetControlOp(nodeID, controlID, elem, numValues, values));
    return tzpl_errNone;
}

tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, f32 const* values) {
    return recordSetControl(nodeID, controlID, tzpl_kF32, numValues, values);
}
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, f64 const* values) {
    return recordSetControl(nodeID, controlID, tzpl_kF64, numValues, values);
}
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, i32 const* values) {
    return recordSetControl(nodeID, controlID, tzpl_kI32, numValues, values);
}
tzpl_SErr setControl(i64 nodeID, i64 controlID, int numValues, i64 const* values) {
    return recordSetControl(nodeID, controlID, tzpl_kI64, numValues, values);
}

tzpl_SErr tapOutlet(i64 nodeID, int outlet, i64 tapID, int mode) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new TapOutletOp(nodeID, outlet, tapID, mode));
    return tzpl_errNone;
}

tzpl_SErr untap(i64 tapID) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new UntapOp(tapID));
    return tzpl_errNone;
}

bool tapExists(Engine* e, i64 tapID) {
    if (!e) return false;
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    return e->taps_.count(tapID) != 0;
}

f32 tapPeak(Engine* e, i64 tapID) {
    if (!e) return 0.f;
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    auto it = e->taps_.find(tapID);
    return it == e->taps_.end() ? 0.f
         : it->second->peak.load(std::memory_order_relaxed);
}

f32 tapRms(Engine* e, i64 tapID) {
    if (!e) return 0.f;
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    auto it = e->taps_.find(tapID);
    return it == e->taps_.end() ? 0.f
         : it->second->rms.load(std::memory_order_relaxed);
}

int tapChans(Engine* e, i64 tapID) {
    if (!e) return 0;
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    auto it = e->taps_.find(tapID);
    return it == e->taps_.end() ? 0 : it->second->chans;
}

int tapDrain(Engine* e, i64 tapID, f32* dst, int maxSamples) {
    if (!e) return 0;
    TapSlot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        auto it = e->taps_.find(tapID);
        if (it == e->taps_.end()) return 0;
        slot = it->second.get();
        // Safe to drain outside the lock ONLY if the slot can't be freed
        // concurrently; drain inside the lock instead -- the FIFO pop is
        // wait-free and cheap, and untap frees slots under this same lock.
        int n = 0;
        f32 v;
        while (n < maxSamples && slot->fifo.pop(v)) dst[n++] = v;
        return n;
    }
}

tzpl_SErr allNotesOff(i64 nodeID) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new NodeCheckedCmdOp(nodeID, new AllNotesOffCmd(nodeID)));
    return tzpl_errNone;
}

tzpl_SErr noteOn(i64 nodeID, int noteID, int length, f32* paramValues) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    // NoteOnCmd copies paramValues into its own vector.
    tBundle.add(new NodeCheckedCmdOp(nodeID, new NoteOnCmd(nodeID, noteID, length, paramValues)));
    return tzpl_errNone;
}

tzpl_SErr noteOff(i64 nodeID, int noteID) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new NodeCheckedCmdOp(nodeID, new NoteOffCmd(nodeID, noteID)));
    return tzpl_errNone;
}

tzpl_SErr noteSetParams(i64 nodeID, int noteID, int n, tzpl_ParamPair* paramValues) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new NodeCheckedCmdOp(nodeID, new NoteSetParamsCmd(nodeID, noteID, n, paramValues)));
    return tzpl_errNone;
}

tzpl_SErr noteSetParamRange(i64 nodeID, int noteID, int first, int length, f32* paramValues) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new NodeCheckedCmdOp(nodeID,
        new NoteSetParamRangeCmd(nodeID, noteID, first, length, paramValues)));
    return tzpl_errNone;
}

// -- Buffer commands --

tzpl_SErr resizeBuffer(i64 nodeID, i64 bufID, int numChannels, i64 length) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new PrebuiltCmdOp(new ResizeBufferCmd(nodeID, bufID, numChannels, length)));
    return tzpl_errNone;
}

tzpl_SErr loadBuffer(i64 nodeID, i64 bufID, const char* path,
                     int channelOffset, i64 frameOffset, i64 numFrames) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    // LoadBufferCmd reads the file in its constructor, i.e. at record time.
    tBundle.add(new PrebuiltCmdOp(new LoadBufferCmd(nodeID, bufID, path,
                                                    channelOffset, frameOffset, numFrames)));
    return tzpl_errNone;
}

tzpl_SErr replaceBuffer(i64 nodeID, i64 bufID, tzpl_Buffer* buffer) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new PrebuiltCmdOp(new ReplaceBufferCmd(nodeID, bufID, buffer)));
    return tzpl_errNone;
}

}
