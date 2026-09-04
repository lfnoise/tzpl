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
#include "tzpl_sample_bank.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <string_view>
#include <vector>
#include <dlfcn.h> // dlopen, dlclose
#include <filesystem>
#include <chrono>
#include <thread>

namespace engine {

//=============================================================================================
#pragma mark CLIENT INTERFACE IMPLEMENTATION

void uninitAudio(Engine* e);

void NullAudioBackend::printDevices() {
    printf("no audio device (deviceless session)\n");
}

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

// Shared input -- one process-global table (there is one mouse per machine),
// shared by every Engine including NRT render engines. Non-RT threads write
// slots with plain stores; plugin graphs read them at audio rate. Aligned
// 32-bit accesses are tear-free, so no locks or FIFOs are involved.
static tzpl_SharedInput gSharedInput; // zero-initialized

tzpl_SharedInput* sharedInput() {
    return &gSharedInput;
}

tzpl_SErr setSharedInput(int slot, f32 value) {
    if (slot < 0 || slot >= TZPL_SHARED_INPUT_SLOTS) return tzpl_errInputOutOfRange;
    gSharedInput.vals[slot] = value;
    return tzpl_errNone;
}

f32 getSharedInput(int slot) {
    if (slot < 0 || slot >= TZPL_SHARED_INPUT_SLOTS) return 0.f;
    return gSharedInput.vals[slot];
}

// Read a loaded plugin's ABI version stamp into `out`. Returns false when the
// symbol is absent: that is NOT version 0, it means the plugin predates
// versioning and its tzpl_SynthDef layout is unknowable (see the header).
// Callers must refuse those rather than reading their structs.
static bool pluginAbiVersion(void* handle, i64& out) {
    if (void* ptr = dlsym(handle, "tzpl_abi_version")) {
        out = *(int64_t*)ptr;
        return true;
    }
    return false;
}

// Shared gate for both load paths. Returns false (and explains) for a plugin
// this engine must not read. `path` is used only for diagnostics.
static bool pluginAbiAcceptable(void* handle, char const* path, bool verbose) {
    i64 version = 0;
    if (!pluginAbiVersion(handle, version)) {
        if (verbose) {
            fprintf(stderr, "*** ERROR: plugin '%s' predates ABI versioning "
                    "(no tzpl_abi_version symbol) and cannot be loaded safely; "
                    "rebuild it\n", path);
        }
        return false;
    }
    if (version > TZPL_PLUGIN_ABI_VERSION) {
        if (verbose) {
            fprintf(stderr, "*** ERROR: plugin '%s' ABI version %lld is newer than "
                    "this engine supports (%d)\n",
                    path, (long long)version, TZPL_PLUGIN_ABI_VERSION);
        }
        return false;
    }
    // Version 2 appended loop fields to tzpl_SampleBankEntry, changing the
    // samples[] stride. Only plugins that index bank entries are affected, and
    // exactly those export "swapSampleBank" (see the header's version notes).
    if (version < 2 && dlsym(handle, "swapSampleBank")) {
        if (verbose) {
            fprintf(stderr, "*** ERROR: plugin '%s' (ABI version %lld) uses sample "
                    "banks, whose entry layout changed in version 2; rebuild it\n",
                    path, (long long)version);
        }
        return false;
    }
    return true;
}

// Validate one count/array pair coming from a plugin before walking it. The
// counts and base pointers are plugin-supplied and entirely untrusted: the ABI
// admits hand-written plugins, and a stale or corrupt one can present a
// non-zero count beside a null base. `what` names the array for diagnostics.
static bool validPluginArray(int count, void const* base, char const* what,
                             char const* name) {
    constexpr int kMaxPluginArray = 4096;
    if (count < 0 || count > kMaxPluginArray) {
        fprintf(stderr, "*** ERROR: plugin def '%s' reports %d %s (out of range)\n",
                name ? name : "?", count, what);
        return false;
    }
    if (count > 0 && !base) {
        fprintf(stderr, "*** ERROR: plugin def '%s' reports %d %s but a null array\n",
                name ? name : "?", count, what);
        return false;
    }
    return true;
}

// Every count/array pair a tzpl_SynthDef (plus its optional buffer, tag and
// sample bank lists) exposes. Checked up front so no walk can run off a null
// base.
static bool validSynthDefArrays(tzpl_SynthDef const& def,
                                tzpl_BufferDefList const* bufs,
                                tzpl_TagList const* tags,
                                tzpl_SampleBankDefList const* banks = nullptr) {
    char const* n = def.name;
    return validPluginArray(def.num_ins, def.ins, "inputs", n)
        && validPluginArray(def.num_outs, def.outs, "outputs", n)
        && validPluginArray(def.num_controls, def.controls, "controls", n)
        && (!bufs || validPluginArray(bufs->num_buffers, bufs->buffers, "buffers", n))
        && (!tags || validPluginArray(tags->num_tags, tags->tags, "tags", n))
        && (!banks || validPluginArray(banks->num_banks, banks->banks, "sample banks", n));
}

bool loadOneDef(Engine* e, const char* path) {
    void* handle = dlopen(path, RTLD_NOW);

    if (!handle) {
        fprintf(stderr, "*** ERROR: dlopen '%s' err '%s'\n", path, dlerror());
        fprintf(stdout, "*** ERROR: dlopen '%s' err '%s'\n", path, dlerror());
        dlclose(handle);
        return false;
    }

    if (!pluginAbiAcceptable(handle, path, /*verbose=*/true)) {
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

    tzpl_SynthDef def = (*(tzpl_LoadSynthDefFun)ptr)();

    // Optional symbols: absent for plugins without sample buffers / tags /
    // sample banks (or built before the symbols existed).
    tzpl_BufferDefList bufs{0, nullptr};
    if (void* bufPtr = dlsym(handle, "loadBufferDefs")) {
        bufs = (*(tzpl_LoadBufferDefsFun)bufPtr)();
    }
    tzpl_TagList tags{0, nullptr};
    if (void* tagPtr = dlsym(handle, "loadTags")) {
        tags = (*(tzpl_LoadTagsFun)tagPtr)();
    }
    tzpl_SampleBankDefList banks{0, nullptr};
    if (void* bankPtr = dlsym(handle, "loadSampleBankDefs")) {
        banks = (*(tzpl_LoadSampleBankDefsFun)bankPtr)();
    }
    auto swapBank = (tzpl_SwapSampleBankFun)dlsym(handle, "swapSampleBank");

    // Refuse a def whose counts and arrays disagree rather than registering
    // bogus pointers with the engine -- addSynthDef reinterpret_casts ins/outs
    // straight into NodeDefInfo, so a bad one would fault on the RT thread
    // long after this call.
    if (!validSynthDefArrays(def, &bufs, &tags, &banks)) {
        fprintf(stderr, "*** ERROR: refusing malformed plugin '%s'\n", path);
        dlclose(handle);
        return false;
    }

    addSynthDef(e, def, handle, &bufs, &tags, &banks, swapBank);
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

void masterMute(Engine* e, bool mute) {
    if (e) e->masterMuted_ = mute;
}

bool masterMuted(Engine* e) {
    return e && e->masterMuted_;
}

void safetyLimiter(Engine* e, Enable onoff) {
    e->enableSafetyLimiter_ = onoff;
}

f32 masterGain(Engine* e) {
    return e ? e->masterGain_ : 1.f;
}

bool safetyLimiterEnabled(Engine* e) {
    return e ? e->enableSafetyLimiter_ == kOn : true;
}

// Shared dispatch: signals worker silos, runs silo 0 on the calling thread,
// applies the safety limiter, and advances anchorSampleTime_. Used by both
// the backend RT callbacks and the NRT renderer.
void processAudioBlock(Engine* e, f32 const* in, f32* out,
                       unsigned int numFrames, f64 streamTime) {
    bool timing = e->statsEnabled_.load(std::memory_order_relaxed);
    auto t0 = timing ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};

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

    f32 gain = (e->masterMuted_ ? 0.f : e->masterGain_) * e->muteGain_;
    e->safetyLimiter_->process(out, e->enableSafetyLimiter_, gain);

    // Measured on `out` AFTER the limiter, so this is what the device plays.
    // Note the limiter's enabled path swaps in the previous block, so with the
    // limiter on the master meter trails the node taps by exactly one block.
    e->masterMeter_.processBlock(out, (int)numFrames, e->streamParams_.channels);
    e->processMasterTaps(out, (int)numFrames, e->streamParams_.channels);

    e->anchorSampleTime_ += numFrames;

    if (timing) {
        auto t1 = std::chrono::steady_clock::now();
        auto nanos = (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(
            t1 - t0).count();
        auto& st = e->stats_;
        u32 epoch = st.statsEpoch.load(std::memory_order_relaxed);
        publishBlockNanos(st, nanos, epoch);
        st.blockCount.fetch_add(1, std::memory_order_relaxed);
        u64 budget = st.budgetNanos.load(std::memory_order_relaxed);
        if (budget && nanos > budget) {
            st.overBudgetCount.fetch_add(1, std::memory_order_relaxed);
            st.dropoutCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
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

    // Sized from the format the backend actually negotiated.
    e->configureStats();

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

    // Clear captured audio state (delay lines, filter memories, active
    // voices, the limiter's lookahead block) so a later startAudio begins
    // from silence instead of replaying the old tail. The backend is
    // stopped, so node state is safe to touch from this thread.
    for (Silo& s : e->silos_) {
        for (Node* head : s.rt_nodeTable_) {
            for (Node* n = head; n; n = n->rt_list.next) n->reset();
        }
    }
    if (e->safetyLimiter_) e->safetyLimiter_->clearState();
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

void addSynthDef(Engine* e, tzpl_SynthDef const& def, void* dlHandle,
                 tzpl_BufferDefList const* bufs, tzpl_TagList const* tags,
                 tzpl_SampleBankDefList const* banks,
                 tzpl_SwapSampleBankFun swapSampleBank) {
    // Optional shared-input symbol: repoint the plugin's all-zero fallback at
    // the process-global table so its graph reads live values. This is the
    // one choke point every plugin handle passes through (loadOneDef and the
    // bridge's compile-and-register paths both land here). Idempotent.
    if (dlHandle) {
        if (void* siPtr = dlsym(dlHandle, "tzpl_sharedInput")) {
            *(tzpl_SharedInput const**)siPtr = &gSharedInput;
        }
    }

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

    // tzpl_BufferDef and BufferInfo have identical layout.
    if (bufs && bufs->num_buffers > 0) {
        info.num_buffers = bufs->num_buffers;
        info.buffers = reinterpret_cast<BufferInfo*>(bufs->buffers);
    }

    if (tags && tags->num_tags > 0) {
        info.num_tags = tags->num_tags;
        info.tags = tags->tags;
    }

    // tzpl_SampleBankDef and SampleBankInfo have identical layout.
    if (banks && banks->num_banks > 0) {
        info.num_banks = banks->num_banks;
        info.banks = reinterpret_cast<SampleBankInfo*>(banks->banks);
    }
    info.swapSampleBank = swapSampleBank;

    addNodeDef(e, info, dlHandle);
    // Def registration changes the port/control metadata graph pollers join
    // by name (hot-reload), so force them to re-snapshot.
    e->graphGeneration_.fetch_add(1, std::memory_order_release);
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
        out.push_back({c.name ? c.name : "", c.controlID, c.spec, c.type});
    }
    return true;
}

// Copy a def's full metadata. Caller must hold e->nrt_lock_.
static void copyDefDesc(NodeDef const* def, DefDesc& out) {
    NodeDefInfo const& info = def->info_;
    out.name = info.name;
    out.ins.clear();
    out.outs.clear();
    out.controls.clear();
    out.buffers.clear();
    out.banks.clear();
    for (int i = 0; i < info.num_ins; ++i) {
        PortInfo const& p = info.ins[i];
        out.ins.push_back({p.name ? p.name : "", p.type});
    }
    for (int i = 0; i < info.num_outs; ++i) {
        PortInfo const& p = info.outs[i];
        out.outs.push_back({p.name ? p.name : "", p.type});
    }
    for (int i = 0; i < info.num_controls; ++i) {
        ControlInfo const& c = info.controls[i];
        out.controls.push_back({c.name ? c.name : "", c.controlID, c.spec, c.type});
    }
    for (int i = 0; i < info.num_buffers; ++i) {
        BufferInfo const& b = info.buffers[i];
        out.buffers.push_back({b.name ? b.name : "", b.type, b.bufID});
    }
    for (int i = 0; i < info.num_banks; ++i) {
        SampleBankInfo const& b = info.banks[i];
        out.banks.push_back({b.name ? b.name : "", b.bankID});
    }
    for (int i = 0; i < info.num_tags; ++i) {
        out.tags.emplace_back(info.tags[i] ? info.tags[i] : "");
    }
}

bool getDefDesc(Engine* e, const char* defName, DefDesc& out) {
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
    copyDefDesc(def, out);
    return true;
}

void listDefDescs(Engine* e, std::vector<DefDesc>& out) {
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        for (u32 bin = 0; bin < kHashBins; ++bin) {
            for (NodeDef* def = e->defs_[bin]; def; def = def->next_) {
                if (def->superseded_) continue;
                out.emplace_back();
                copyDefDesc(def, out.back());
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](DefDesc const& a, DefDesc const& b) { return a.name < b.name; });
}

// ============================================================================
// Live-graph snapshot (graph view). Reads only the NRT topology shadow --
// never Node* -- under shadowMtx_, so it cannot race node deletion.

u64 graphGeneration(Engine* e) {
    return e->graphGeneration_.load(std::memory_order_acquire);
}

int numSilos(Engine* e) {
    return (int)e->silos_.size();
}

bool getGraphDesc(Engine* e, int silo, GraphDesc& out) {
    if (silo < 0 || silo >= (int)e->silos_.size()) return false;
    Silo const& s = e->silos_[silo];

    out.nodes.clear();
    out.conns.clear();
    {
        std::lock_guard<std::mutex> lck(e->shadowMtx_);
        // Sample the generation inside the lock so it is coherent with the
        // copied content (a commit bumps it under the same lock's edits).
        out.generation = e->graphGeneration_.load(std::memory_order_acquire);
        out.nodes.reserve(s.shadow_.nodes.size());
        for (auto const& [nodeID, defName] : s.shadow_.nodes)
            out.nodes.push_back({nodeID, defName});
        out.conns.reserve(s.shadow_.conns.size());
        for (ShadowConn const& c : s.shadow_.conns)
            out.conns.push_back({c.srcNode, c.srcPort, c.dstNode, c.dstPort});
    }
    std::sort(out.nodes.begin(), out.nodes.end(),
              [](LiveNodeDesc const& a, LiveNodeDesc const& b) {
                  return a.nodeID < b.nodeID;
              });
    return true;
}

// ============================================================================
// On-disk plugin discovery (plugin browser).

// Split a plugin filename stem into (name, revision): "<name>_synth" -> rev 0,
// "<name>_synth_rN" -> rev N. Non-conforming stems keep the whole stem as the
// name (loadDefs loads any dylib, so list them too).
static void parsePluginStem(std::string const& stem, std::string& name, u64& rev) {
    name = stem;
    rev = 0;
    std::string_view s = stem;
    if (auto rpos = s.rfind("_r"); rpos != std::string_view::npos
        && rpos + 2 < s.size()
        && s.find_first_not_of("0123456789", rpos + 2) == std::string_view::npos) {
        rev = std::stoull(std::string(s.substr(rpos + 2)));
        s = s.substr(0, rpos);
    }
    constexpr std::string_view kSuffix = "_synth";
    if (s.size() > kSuffix.size() && s.ends_with(kSuffix)) {
        s.remove_suffix(kSuffix.size());
    }
    name = std::string(s);
}

void listPluginFiles(std::vector<std::string> const& dirs,
                     std::vector<PluginFile>& out) {
    namespace fs = std::filesystem;
    struct Entry {
        std::string path;
        fs::file_time_type mtime;
        u64 rev;
        usize dirIndex;
    };
    std::unordered_map<std::string, Entry> best;

    for (usize di = 0; di < dirs.size(); ++di) {
        std::error_code ec;
        fs::recursive_directory_iterator iter(dirs[di], ec);
        if (ec) continue;
        for (auto& p : iter) {
            if (!p.is_regular_file()
                || fs::path(p.path()).extension() != kPluginExt) continue;
            std::string name;
            u64 rev;
            parsePluginStem(fs::path(p.path()).stem().string(), name, rev);
            std::error_code mec;
            auto mtime = fs::last_write_time(p.path(), mec);
            if (mec) mtime = fs::file_time_type::min();
            Entry cand{p.path().string(), mtime, rev, di};
            auto it = best.find(name);
            if (it == best.end()) {
                best.emplace(name, std::move(cand));
            } else if (it->second.dirIndex == di
                       && (mtime > it->second.mtime
                           || (mtime == it->second.mtime
                               && rev > it->second.rev))) {
                // Same dir, newer build. Mtime decides, not the revision
                // number: revisions only order builds within one process, so a
                // higher revision left by an earlier session is not newer.
                // Rev breaks ties when timestamps are identical.
                // An earlier dir always shadows.
                it->second = std::move(cand);
            }
        }
    }

    for (auto& [name, e] : best) {
        out.push_back({name, std::move(e.path)});
    }
    std::sort(out.begin(), out.end(),
              [](PluginFile const& a, PluginFile const& b) { return a.name < b.name; });
}

// Build a DefDesc directly from the ABI structs a plugin's load() returned.
// Callers must have run validSynthDefArrays first: every loop below trusts
// the count/base pair it walks.
static void defDescFromSynthDef(tzpl_SynthDef const& def,
                                tzpl_BufferDefList const& bufs,
                                tzpl_TagList const& tags,
                                tzpl_SampleBankDefList const& banks, DefDesc& out) {
    out.name = def.name ? def.name : "";
    for (int i = 0; i < def.num_ins; ++i) {
        tzpl_PortDef const& p = def.ins[i];
        out.ins.push_back({p.name ? p.name : "", p.type});
    }
    for (int i = 0; i < def.num_outs; ++i) {
        tzpl_PortDef const& p = def.outs[i];
        out.outs.push_back({p.name ? p.name : "", p.type});
    }
    for (int i = 0; i < def.num_controls; ++i) {
        tzpl_ControlDef const& c = def.controls[i];
        out.controls.push_back({c.name ? c.name : "",
                                static_cast<i64>(c.id), c.spec, c.type});
    }
    for (int i = 0; i < bufs.num_buffers; ++i) {
        tzpl_BufferDef const& b = bufs.buffers[i];
        out.buffers.push_back({b.name ? b.name : "", b.type, b.bufID});
    }
    for (int i = 0; i < banks.num_banks; ++i) {
        tzpl_SampleBankDef const& b = banks.banks[i];
        out.banks.push_back({b.name ? b.name : "", b.bankID});
    }
    for (int i = 0; i < tags.num_tags; ++i) {
        out.tags.emplace_back(tags.tags[i] ? tags.tags[i] : "");
    }
}

bool getPluginFileDesc(char const* path, DefDesc& out) {
    namespace fs = std::filesystem;

    // Cache by path + mtime (failures too), so browsing never dlopens the
    // same plugin version twice.
    struct CacheEntry {
        fs::file_time_type mtime;
        bool ok;
        DefDesc desc;
    };
    static std::mutex cacheMtx;
    static std::unordered_map<std::string, CacheEntry> cache;

    std::error_code ec;
    auto mtime = fs::last_write_time(path, ec);
    if (ec) return false;

    std::lock_guard<std::mutex> lck(cacheMtx);
    auto it = cache.find(path);
    if (it != cache.end() && it->second.mtime == mtime) {
        if (!it->second.ok) return false;
        out = it->second.desc;
        return true;
    }

    CacheEntry entry{mtime, false, {}};
    if (void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        // Unstamped (pre-versioning) or newer-ABI plugin: its structs may not
        // match ours, so we must not read them. The cached failure keeps the
        // browser from probing it again on every selection change. Quiet here:
        // browsing a directory of mixed plugins should not spam the log.
        if (!pluginAbiAcceptable(handle, path, /*verbose=*/false)) {
            dlclose(handle);
            cache[path] = std::move(entry);
            return false;
        }
        if (void* ptr = dlsym(handle, "load")) {
            tzpl_SynthDef def = (*(tzpl_LoadSynthDefFun)ptr)();
            tzpl_BufferDefList bufs{0, nullptr};
            if (void* bufPtr = dlsym(handle, "loadBufferDefs")) {
                bufs = (*(tzpl_LoadBufferDefsFun)bufPtr)();
            }
            tzpl_TagList tags{0, nullptr};
            if (void* tagPtr = dlsym(handle, "loadTags")) {
                tags = (*(tzpl_LoadTagsFun)tagPtr)();
            }
            tzpl_SampleBankDefList banks{0, nullptr};
            if (void* bankPtr = dlsym(handle, "loadSampleBankDefs")) {
                banks = (*(tzpl_LoadSampleBankDefsFun)bankPtr)();
            }
            if (validSynthDefArrays(def, &bufs, &tags, &banks)) {
                defDescFromSynthDef(def, bufs, tags, banks, entry.desc);
                entry.ok = true;
            }
        }
        dlclose(handle);
    }
    auto& stored = cache[path] = std::move(entry);
    if (!stored.ok) return false;
    out = stored.desc;
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

// ============================================================================
// Graph shadow journal (graph view). Topology-mutating ops record ShadowEdits
// while they validate at submit; on success sched() appends one
// ShadowCommitCmd carrying the journal as the bundle's LAST command. The
// commit applies its edits to the silo's GraphShadow in doNRT -- after the
// bundle actually executed -- so the shadow follows RT execution order even
// when bundles are scheduled to run out of submission order, or tempo
// changes on the per-silo clocks reorder clock-scheduled bundles. A bundle
// that fails validation discards its journal with its commands (atomic
// abort); a schedOnTimeOnly bundle dropped for lateness never fires its
// commit (fired_ guard).
// ============================================================================

struct ShadowEdit {
    enum Op : u8 {
        AddNode,             // nodeID, defName
        RemoveNode,          // nodeID
        AddConn,             // conn (skipped if either endpoint is gone)
        DisconnectSrc,       // conn: mirrors DisconnectSourceCmd (see commit)
        RemoveConnsToDst,    // conn.dstNode/dstPort
        RemoveConnsFromSrc,  // conn.srcNode/srcPort
        RemoveConnsTouching, // nodeID
        RewriteSrc,          // conn: (srcNode,srcPort)=old, (dstNode,dstPort)=new
        Clear,               // keep nodes 0/1 and conns between them
    };
    Op op;
    ShadowConn conn{};
    i64 nodeID = 0;
    std::string defName;
};
using ShadowEdits = std::vector<ShadowEdit>;

// Apply journaled edits to a silo's shadow. Caller holds Engine::shadowMtx_.
// Deliberately tolerant of stale state (mirroring the RT side's silent
// no-ops when a scheduled command's target vanished before it executed).
static void commitShadowEdits(GraphShadow& sh, ShadowEdits const& edits) {
    for (ShadowEdit const& ed : edits) {
        switch (ed.op) {
            case ShadowEdit::AddNode:
                sh.nodes[ed.nodeID] = ed.defName;
                break;
            case ShadowEdit::RemoveNode:
                sh.nodes.erase(ed.nodeID);
                break;
            case ShadowEdit::AddConn:
                // Mirror ConnectCmd::doRT: no-op if either node is gone by
                // the time the bundle executes.
                if (sh.nodes.count(ed.conn.srcNode) && sh.nodes.count(ed.conn.dstNode))
                    sh.conns.push_back(ed.conn);
                break;
            case ShadowEdit::DisconnectSrc: {
                // Mirror DisconnectSourceCmd::doRT: with fan-in (2+ sources
                // into the inlet) remove one slot matching src; with a single
                // direct connection RT disconnects the inlet regardless of
                // which source is named.
                auto toDst = [&](ShadowConn const& c) {
                    return c.dstNode == ed.conn.dstNode && c.dstPort == ed.conn.dstPort;
                };
                auto n = std::count_if(sh.conns.begin(), sh.conns.end(), toDst);
                if (n >= 2) {
                    auto it = std::find(sh.conns.begin(), sh.conns.end(), ed.conn);
                    if (it != sh.conns.end()) sh.conns.erase(it);
                } else {
                    std::erase_if(sh.conns, toDst);
                }
                break;
            }
            case ShadowEdit::RemoveConnsToDst:
                std::erase_if(sh.conns, [&](ShadowConn const& c) {
                    return c.dstNode == ed.conn.dstNode && c.dstPort == ed.conn.dstPort;
                });
                break;
            case ShadowEdit::RemoveConnsFromSrc:
                std::erase_if(sh.conns, [&](ShadowConn const& c) {
                    return c.srcNode == ed.conn.srcNode && c.srcPort == ed.conn.srcPort;
                });
                break;
            case ShadowEdit::RemoveConnsTouching:
                std::erase_if(sh.conns, [&](ShadowConn const& c) {
                    return c.srcNode == ed.nodeID || c.dstNode == ed.nodeID;
                });
                break;
            case ShadowEdit::RewriteSrc:
                for (ShadowConn& c : sh.conns) {
                    if (c.srcNode == ed.conn.srcNode && c.srcPort == ed.conn.srcPort) {
                        c.srcNode = ed.conn.dstNode;
                        c.srcPort = ed.conn.dstPort;
                    }
                }
                break;
            case ShadowEdit::Clear:
                // Mirror Silo::removeAllNodes: nodes 0/1 survive, and so does
                // any direct wire between them.
                std::erase_if(sh.nodes, [](auto const& kv) {
                    return kv.first != 0 && kv.first != 1;
                });
                std::erase_if(sh.conns, [](ShadowConn const& c) {
                    return !((c.srcNode == 0 || c.srcNode == 1) &&
                             (c.dstNode == 0 || c.dstNode == 1));
                });
                break;
        }
    }
}

struct ShadowCommitCmd : Command
{
    ShadowEdits edits_;
    bool fired_ = false;

    explicit ShadowCommitCmd(ShadowEdits&& edits) : edits_(std::move(edits)) {}

    void doRT(Silo*) override {
        // err_ is already tzpl_errTooLate when a schedOnTimeOnly bundle was
        // dropped without running -- in that case never commit.
        if (err_ == tzpl_errNone) fired_ = true;
    }
    bool doNRT(Silo* s) override {
        if (fired_) {
            Engine* e = s->engine_;
            std::lock_guard<std::mutex> lck(e->shadowMtx_);
            commitShadowEdits(s->shadow_, edits_);
            // Bump inside the lock so getGraphDesc (which samples the
            // generation under the same lock) stays coherent with content.
            e->graphGeneration_.fetch_add(1, std::memory_order_release);
        }
        return true;
    }
};

struct BundleOp
{
    BundleOp* next_ = nullptr;
    virtual ~BundleOp() = default;
    // Validate against the chosen silo, append the materialized Command(s)
    // to `out`, and journal any topology changes into `edits`. Runs on the
    // submitting (NRT) thread.
    virtual tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) = 0;
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
                              ShadowEdits& edits,
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
    edits.push_back({ShadowEdit::AddNode, {}, nodeID, def->info_.name});
    return tzpl_errNone;
}

static tzpl_SErr applyConnect(Engine* e, Silo* s, CommandList& out,
                              ShadowEdits& edits,
                              PortAddr src, PortAddr dst,
                              f64 xfadeTime, FadeCurve curve) {
    tzpl_SErr err;
    OutPort* srcPort;
    err = s->nrt_getOutPort(src, srcPort);
    if (err != tzpl_errNone) return err;

    InPort* dstPort;
    err = s->nrt_getInPort(dst, dstPort);
    if (err != tzpl_errNone) return err;

    // Element type and rate must match exactly; differing channel counts are
    // adapted by a hidden node (see tzpl_chanadapt.hpp).
    {
        tzpl_SErr err = relaxedCompatibleTypes(srcPort->type_, dstPort->type_);
        if (err != tzpl_errNone) return err;
    }

    // Pre-allocate the hidden nodes this connection may need. All of them are
    // built in NRT (allocation is forbidden on the RT thread) and whichever
    // the RT thread does not use is handed straight back for reclamation.
    //
    // The adapter sits closest to the source, so everything downstream of it
    // -- crossfader, mixer, destination -- speaks the destination's type.
    tzpl_SignalType type = dstPort->type_;
    Node* adaptNode = nullptr;
    if (srcPort->type_.chans != type.chans) {
        adaptNode = newChanAdaptNode(e, s, srcPort->type_, type);
    }

    // The mixer is always pre-allocated (cheap). The RT thread decides
    // whether to use it based on the actual connection state at execution
    // time. This avoids NRT/RT race conditions -- the NRT thread cannot
    // reliably know whether previous connects have been applied yet.
    Node* mixerNode = newMixerNode(e, s, type, 4);

    Node* xfaderNode = nullptr;
    if (xfadeTime > 0. && isFloat(type.elem)) {
        xfaderNode = newXFaderNode(e, s, xfadeTime, curve, type);
    }
    out.add(new ConnectCmd(src, dst, xfaderNode, curve, mixerNode, adaptNode));
    edits.push_back({ShadowEdit::AddConn,
                     {src.nodeID, src.index, dst.nodeID, dst.index}});
    return tzpl_errNone;
}

static tzpl_SErr applyReconnectOutput(Engine* e, Silo* s, CommandList& out,
                                      ShadowEdits& edits,
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
    edits.push_back({ShadowEdit::RewriteSrc,
                     {oldSrc.nodeID, oldSrc.index, newSrc.nodeID, newSrc.index}});
    return tzpl_errNone;
}

// True when inlet `dst` is already fed by `src` -- directly or through its
// fan-in mixer. Used by applyReplaceNode to make the input copy idempotent:
// without this, replacing back to a node whose inputs were never
// disconnected re-adds the same connection as a second mixer slot, silently
// double-summing the signal (and again on every subsequent swap).
static bool inletHasSource(InPort const& dst, OutPort const* src) {
    if (adaptedFrom(dst.srcPort_) == src) return true;
    if (dst.mixerNode_) {
        return (bool)findMixerSlot(dst.mixerNode_, const_cast<OutPort*>(src));
    }
    return false;
}

static tzpl_SErr applyReplaceNode(Engine* e, Silo* s, CommandList& out,
                                  ShadowEdits& edits,
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
            // Every source in the inlet's mixer chain, not just the head's.
            static constexpr int kMaxFanIn = 256;
            OutPort* srcs[kMaxFanIn];
            int n = collectMixerSources(oldNode->ins[i].mixerNode_, srcs, kMaxFanIn);
            for (int k = 0; k < n; ++k) {
                if (srcs[k]->node_->nodeID >= 0
                    && !inletHasSource(newNode->ins[i], srcs[k])) {
                    applyConnect(e, s, out, edits,
                            {srcs[k]->node_->nodeID, srcs[k]->index_},
                            {newNodeID, i}, 0., fadeLinear);
                }
            }
        } else {
            OutPort* src = adaptedFrom(oldNode->ins[i].srcPort_);
            if (src && src->node_->nodeID >= 0
                && !inletHasSource(newNode->ins[i], src)) {
                applyConnect(e, s, out, edits,
                        {src->node_->nodeID, src->index_}, {newNodeID, i},
                        0., fadeLinear);
            }
        }
    }

    // reconnect all outputs
    for (int i = 0; i < oldNode->outs.size(); ++i) {
        applyReconnectOutput(e, s, out, edits, {oldNodeID, i}, {newNodeID, i}, xfadeTime, curve);
    }

    return tzpl_errNone;
}

static tzpl_SErr applyDisconnectInput(Engine* e, Silo* s, CommandList& out,
                                      ShadowEdits& edits,
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
    edits.push_back({ShadowEdit::RemoveConnsToDst, {0, 0, dst.nodeID, dst.index}});
    return tzpl_errNone;
}

static tzpl_SErr applyDisconnectSource(Engine* e, Silo* s, CommandList& out,
                                       ShadowEdits& edits,
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
    edits.push_back({ShadowEdit::DisconnectSrc,
                     {src.nodeID, src.index, dst.nodeID, dst.index}});
    return tzpl_errNone;
}

static tzpl_SErr applyDisconnectOutput(Engine*, Silo* s, CommandList& out,
                                       ShadowEdits& edits, PortAddr src) {
    OutPort* port;
    tzpl_SErr err = s->nrt_getOutPort(src, port);
    if (err != tzpl_errNone) return err;

    out.add(new DisconnectOutputCmd(src));
    edits.push_back({ShadowEdit::RemoveConnsFromSrc, {src.nodeID, src.index, 0, 0}});
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
                               ShadowEdits&, // setInput never unlinks a wire
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
                                ShadowEdits&,
                                i64 nodeID, int outlet, i64 tapID, int mode,
                                int ownerKind, int ownerSilo) {
    OutPort* port;
    tzpl_SErr err = s->nrt_getOutPort(PortAddr{nodeID, outlet}, port);
    if (err != tzpl_errNone) return err;
    if (port->type_.elem != tzpl_kF32) return tzpl_errTypeMismatch;
    if (mode != tapMeter && mode != tapScope) return tzpl_errNotImplemented;

    TapSlot* slot;
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        // Refuse here rather than letting installTap fail on RT: a failed
        // install returns errNone from go(), leaving the caller holding a
        // tapID that reads silence forever. Counting registry entries is
        // conservative -- a tap whose untap is submitted but not yet executed
        // still counts -- which is the right side to err on.
        int live = 0;
        for (auto const& [id, ts] : e->taps_) {
            if (ts->silo == s->index_) ++live;
        }
        if (live >= Silo::kMaxTaps) return tzpl_errResourceLimit;

        auto [it, inserted] = e->taps_.try_emplace(
            tapID, std::make_unique<TapSlot>(static_cast<TapMode>(mode)));
        if (!inserted) return tzpl_errAlreadyAdded;
        slot = it->second.get();
        slot->tapID = tapID;
        slot->silo = s->index_;
        slot->ownerKind = (TapOwnerKind)ownerKind;
        slot->ownerSilo = ownerSilo;
        slot->chans = std::min(port->type_.chans,
                               (decltype(port->type_.chans))TapSlot::kMaxScopeChans);
        if (slot->chans < 1) slot->chans = 1;
    }
    out.add(new TapOutletCmd(e, nodeID, outlet, tapID, slot));
    return tzpl_errNone;
}

static tzpl_SErr applyTapMaster(Engine* e, Silo* s, CommandList& out,
                                ShadowEdits&, i64 tapID, int mode,
                                int ownerKind, int ownerSilo) {
    // The master bus is summed and limited on silo 0's thread, so that is the
    // only thread allowed to touch the master tap table.
    if (s->index_ != 0) return tzpl_errSiloOutOfRange;
    if (mode != tapMeter && mode != tapScope) return tzpl_errNotImplemented;

    TapSlot* slot;
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        int live = 0;
        for (auto const& [id, ts] : e->taps_) {
            if (ts->isMaster) ++live;
        }
        if (live >= Engine::kMaxMasterTaps) return tzpl_errResourceLimit;

        auto [it, inserted] = e->taps_.try_emplace(
            tapID, std::make_unique<TapSlot>(static_cast<TapMode>(mode)));
        if (!inserted) return tzpl_errAlreadyAdded;
        slot = it->second.get();
        slot->tapID = tapID;
        slot->silo = 0;
        slot->isMaster = true;
        slot->ownerKind = (TapOwnerKind)ownerKind;
        slot->ownerSilo = ownerSilo;
        slot->chans = std::min(e->streamParams_.channels,
                               TapSlot::kMaxScopeChans);
        if (slot->chans < 1) slot->chans = 1;
    }
    out.add(new TapMasterCmd(e, tapID, slot));
    return tzpl_errNone;
}

static tzpl_SErr applyUntap(Engine* e, Silo* s, CommandList& out,
                            ShadowEdits&, i64 tapID) {
    bool isMaster = false;
    int tapSilo = 0;
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        auto it = e->taps_.find(tapID);
        if (it == e->taps_.end()) return tzpl_errNodeNotFound;
        isMaster = it->second->isMaster;
        tapSilo = it->second->silo;
    }
    // The removal must run on the thread that owns the table holding this
    // tap: master taps live in the Engine's table, which only silo 0's thread
    // touches; node taps live in their own silo's. Removing from anywhere
    // else would find nothing on RT, then free the slot in doNRT -- leaving
    // the owning silo's tap table pointing at freed memory.
    if (s->index_ != (isMaster ? 0 : tapSilo)) return tzpl_errSiloOutOfRange;
    out.add(new UntapCmd(tapID, isMaster));
    return tzpl_errNone;
}

static tzpl_SErr applySetControl(Engine*, Silo* s, CommandList& out,
                                 ShadowEdits&,
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

// By-name form: resolve the control name against the target node's def, then
// hand off to the by-ID path. Runs at submit, so a node created earlier in
// the same bundle already exists here.
static tzpl_SErr applySetControlByName(Engine* e, Silo* s, CommandList& out,
                                       ShadowEdits& edits,
                                       i64 nodeID, char const* controlName,
                                       tzpl_ElemType srcElem, void const* bytes,
                                       int numValues) {
    Node* node = s->nrt_getNode(nodeID);
    if (!node) return tzpl_errNodeNotFound;

    NodeDefInfo const& info = node->def->info_;
    for (int i = 0; i < info.num_controls; ++i) {
        ControlInfo const& c = info.controls[i];
        if (c.name && strcmp(c.name, controlName) == 0) {
            return applySetControl(e, s, out, edits, nodeID, c.controlID,
                                   srcElem, bytes, numValues);
        }
    }
    return tzpl_errControlNotFound;
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

    tzpl_SErr apply(Engine*, Silo*, CommandList& out, ShadowEdits&) override {
        out.add(cmd_);
        cmd_ = nullptr;
        return tzpl_errNone;
    }
};

// A command that was fully built at record time and whose only silo-side
// validation is that the target node exists (the note/voice commands).
struct NodeCheckedCmdOp : BundleOp
{
    i64 nodeID_;
    Command* cmd_;

    NodeCheckedCmdOp(i64 nodeID, Command* cmd) : nodeID_(nodeID), cmd_(cmd) {}
    ~NodeCheckedCmdOp() override { delete cmd_; } // owned until applied

    tzpl_SErr apply(Engine*, Silo* s, CommandList& out, ShadowEdits&) override {
        if (!s->nrt_getNode(nodeID_)) return tzpl_errNodeNotFound;
        out.add(cmd_);
        cmd_ = nullptr;
        return tzpl_errNone;
    }
};

// freeNode: node-existence check + shadow edits (purge the node and every
// wire touching it, mirroring Silo::removeNode -> disconnectNode).
struct FreeNodeOp : BundleOp
{
    i64 nodeID_;
    Command* cmd_;

    FreeNodeOp(i64 nodeID, Command* cmd) : nodeID_(nodeID), cmd_(cmd) {}
    ~FreeNodeOp() override { delete cmd_; } // owned until applied

    tzpl_SErr apply(Engine*, Silo* s, CommandList& out, ShadowEdits& edits) override {
        if (!s->nrt_getNode(nodeID_)) return tzpl_errNodeNotFound;
        out.add(cmd_);
        cmd_ = nullptr;
        edits.push_back({ShadowEdit::RemoveConnsTouching, {}, nodeID_});
        edits.push_back({ShadowEdit::RemoveNode, {}, nodeID_});
        return tzpl_errNone;
    }
};

// disconnectNode: node-existence check + purge every wire touching it.
struct DisconnectNodeOp : BundleOp
{
    i64 nodeID_;
    Command* cmd_;

    DisconnectNodeOp(i64 nodeID, Command* cmd) : nodeID_(nodeID), cmd_(cmd) {}
    ~DisconnectNodeOp() override { delete cmd_; } // owned until applied

    tzpl_SErr apply(Engine*, Silo* s, CommandList& out, ShadowEdits& edits) override {
        if (!s->nrt_getNode(nodeID_)) return tzpl_errNodeNotFound;
        out.add(cmd_);
        cmd_ = nullptr;
        edits.push_back({ShadowEdit::RemoveConnsTouching, {}, nodeID_});
        return tzpl_errNone;
    }
};

// freeAllNodes: prebuilt command + shadow Clear.
struct FreeAllNodesOp : BundleOp
{
    Command* cmd_;

    explicit FreeAllNodesOp(Command* cmd) : cmd_(cmd) {}
    ~FreeAllNodesOp() override { delete cmd_; } // owned until applied

    tzpl_SErr apply(Engine*, Silo*, CommandList& out, ShadowEdits& edits) override {
        out.add(cmd_);
        cmd_ = nullptr;
        edits.push_back({ShadowEdit::Clear});
        return tzpl_errNone;
    }
};

struct NewNodeOp : BundleOp
{
    std::string defName_;
    i64 nodeID_;

    NewNodeOp(char const* defName, i64 nodeID) : defName_(defName), nodeID_(nodeID) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyNewNode(e, s, out, edits, defName_.c_str(), nodeID_);
    }
};

struct ConnectOp : BundleOp
{
    PortAddr src_, dst_;
    f64 xfadeTime_;
    FadeCurve curve_;

    ConnectOp(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve)
        : src_(src), dst_(dst), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyConnect(e, s, out, edits, src_, dst_, xfadeTime_, curve_);
    }
};

struct ReconnectOutputOp : BundleOp
{
    PortAddr oldSrc_, newSrc_;
    f64 xfadeTime_;
    FadeCurve curve_;

    ReconnectOutputOp(PortAddr oldSrc, PortAddr newSrc, f64 xfadeTime, FadeCurve curve)
        : oldSrc_(oldSrc), newSrc_(newSrc), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyReconnectOutput(e, s, out, edits, oldSrc_, newSrc_, xfadeTime_, curve_);
    }
};

struct ReplaceNodeOp : BundleOp
{
    i64 oldNodeID_, newNodeID_;
    f64 xfadeTime_;
    FadeCurve curve_;

    ReplaceNodeOp(i64 oldNodeID, i64 newNodeID, f64 xfadeTime, FadeCurve curve)
        : oldNodeID_(oldNodeID), newNodeID_(newNodeID), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyReplaceNode(e, s, out, edits, oldNodeID_, newNodeID_, xfadeTime_, curve_);
    }
};

struct DisconnectInputOp : BundleOp
{
    PortAddr dst_;
    f64 xfadeTime_;
    FadeCurve curve_;

    DisconnectInputOp(PortAddr dst, f64 xfadeTime, FadeCurve curve)
        : dst_(dst), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyDisconnectInput(e, s, out, edits, dst_, xfadeTime_, curve_);
    }
};

struct DisconnectSourceOp : BundleOp
{
    PortAddr src_, dst_;
    f64 xfadeTime_;
    FadeCurve curve_;

    DisconnectSourceOp(PortAddr src, PortAddr dst, f64 xfadeTime, FadeCurve curve)
        : src_(src), dst_(dst), xfadeTime_(xfadeTime), curve_(curve) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyDisconnectSource(e, s, out, edits, src_, dst_, xfadeTime_, curve_);
    }
};

struct DisconnectOutputOp : BundleOp
{
    PortAddr src_;

    explicit DisconnectOutputOp(PortAddr src) : src_(src) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyDisconnectOutput(e, s, out, edits, src_);
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

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applySetInput(e, s, out, edits, dst_, elem_, bytes_.data(), numValues_,
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

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applySetControl(e, s, out, edits, nodeID_, controlID_, elem_,
                               bytes_.data(), numValues_);
    }
};

struct SetControlByNameOp : BundleOp
{
    i64 nodeID_;
    std::string controlName_;
    tzpl_ElemType elem_;
    int numValues_;
    std::vector<u8> bytes_;

    SetControlByNameOp(i64 nodeID, char const* controlName, tzpl_ElemType elem,
                       int numValues, void const* values)
        : nodeID_(nodeID), controlName_(controlName), elem_(elem),
          numValues_(numValues), bytes_(numValues * elemSize(elem))
    {
        memcpy(bytes_.data(), values, bytes_.size());
    }

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applySetControlByName(e, s, out, edits, nodeID_, controlName_.c_str(),
                                     elem_, bytes_.data(), numValues_);
    }
};

struct TapOutletOp : BundleOp
{
    i64 nodeID_;
    int outlet_;
    i64 tapID_;
    int mode_;
    int ownerKind_;
    int ownerSilo_;

    TapOutletOp(i64 nodeID, int outlet, i64 tapID, int mode,
                int ownerKind, int ownerSilo)
        : nodeID_(nodeID), outlet_(outlet), tapID_(tapID), mode_(mode),
          ownerKind_(ownerKind), ownerSilo_(ownerSilo)
    {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyTapOutlet(e, s, out, edits, nodeID_, outlet_, tapID_, mode_,
                              ownerKind_, ownerSilo_);
    }
};

struct TapMasterOp : BundleOp
{
    i64 tapID_;
    int mode_;
    int ownerKind_;
    int ownerSilo_;

    TapMasterOp(i64 tapID, int mode, int ownerKind, int ownerSilo)
        : tapID_(tapID), mode_(mode), ownerKind_(ownerKind), ownerSilo_(ownerSilo) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyTapMaster(e, s, out, edits, tapID_, mode_,
                              ownerKind_, ownerSilo_);
    }
};

struct UntapOp : BundleOp
{
    i64 tapID_;

    UntapOp(i64 tapID) : tapID_(tapID) {}

    tzpl_SErr apply(Engine* e, Silo* s, CommandList& out, ShadowEdits& edits) override {
        return applyUntap(e, s, out, edits, tapID_);
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
    // including nodes already inserted into the silo's NRT table. The
    // shadow journal is discarded with them (nothing was committed).
    CommandList cmds;
    ShadowEdits edits;
    for (BundleOp* op = tBundle.head; op; op = op->next_) {
        tzpl_SErr err = op->apply(e, s, cmds, edits);
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

    // The shadow commit travels as the bundle's LAST command: it executes
    // when the bundle does (wherever scheduling lands it), so the graph
    // shadow tracks RT execution order.
    if (!edits.empty()) cmds.add(new ShadowCommitCmd(std::move(edits)));

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

bool clockQuery(Engine* e, int clock, f64 targetBeat,
                f64& beatsNow, f64& secsUntil) {
    if (!e || clock < 0 || clock >= e->numTempoClocks_) return false;
    // An NRT engine never reaches AudioState::running (startAudio no-ops)
    // but its clocks DO advance -- the render pump drives sampleTime_ -- so
    // a follower may read them. Only a live engine with audio stopped is a
    // frozen clock.
    if (!e->nrtMode_ && !isAudioRunning(e)) return false;   // clock frozen: follower falls back
    Silo& s = e->silos_[0];
    if (clock >= (int)s.tempoClocks_.size()) return false;
    // Same read discipline as clockBeats/clockTempoBPM: an unlocked snapshot
    // of silo 0's clock; the follower re-checks near its deadline anyway.
    TempoClock const& tc = s.tempoClocks_[clock];
    i64 sampleNow = s.sampleTime_;
    f64 secondsNow = tc.secondsAtSample(sampleNow);
    beatsNow = tc.ramp_.secondsToBeats(secondsNow);
    secsUntil = tc.ramp_.beatsToSeconds(targetBeat) - secondsNow;
    return true;
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
    tBundle.add(new FreeNodeOp(nodeID, new RemoveNodeCmd{nodeID}));
    return tzpl_errNone;
}

tzpl_SErr freeAllNodes() {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new FreeAllNodesOp(new RemoveAllNodesCmd{}));
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
    tBundle.add(new DisconnectNodeOp(nodeID, new DisconnectNodeCmd(nodeID)));
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

static tzpl_SErr recordSetControlByName(i64 nodeID, char const* controlName,
                                        tzpl_ElemType elem, int numValues,
                                        void const* values) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new SetControlByNameOp(nodeID, controlName, elem, numValues, values));
    return tzpl_errNone;
}

tzpl_SErr setControl(i64 nodeID, char const* controlName, int numValues, f32 const* values) {
    return recordSetControlByName(nodeID, controlName, tzpl_kF32, numValues, values);
}
tzpl_SErr setControl(i64 nodeID, char const* controlName, int numValues, f64 const* values) {
    return recordSetControlByName(nodeID, controlName, tzpl_kF64, numValues, values);
}
tzpl_SErr setControl(i64 nodeID, char const* controlName, int numValues, i32 const* values) {
    return recordSetControlByName(nodeID, controlName, tzpl_kI32, numValues, values);
}
tzpl_SErr setControl(i64 nodeID, char const* controlName, int numValues, i64 const* values) {
    return recordSetControlByName(nodeID, controlName, tzpl_kI64, numValues, values);
}

tzpl_SErr tapOutlet(i64 nodeID, int outlet, i64 tapID, int mode,
                    int ownerKind, int ownerSilo) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new TapOutletOp(nodeID, outlet, tapID, mode, ownerKind, ownerSilo));
    return tzpl_errNone;
}

tzpl_SErr tapMaster(i64 tapID, int mode, int ownerKind, int ownerSilo) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new TapMasterOp(tapID, mode, ownerKind, ownerSilo));
    return tzpl_errNone;
}

tzpl_SErr untap(i64 tapID) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new UntapOp(tapID));
    return tzpl_errNone;
}

int freeTapsByOwner(Engine* e, int ownerKind, int ownerSilo) {
    if (!e) return 0;
    // We open our own bundles, so a caller mid-bundle would have theirs
    // clobbered. begin() refuses in that case; bail before touching anything.
    if (begin(e) != tzpl_errNone) return 0;
    go(0);  // discard the probe bundle

    // Group by the silo that must do the removal: a tap can only be untapped
    // from the silo whose table holds it (see applyUntap).
    std::map<int, std::vector<i64>> bySilo;
    {
        std::lock_guard<std::mutex> lck(e->nrt_lock_);
        for (auto const& [id, slot] : e->taps_) {
            if (ownerKind != tapOwnerAny) {
                if (slot->ownerKind != ownerKind) continue;
                if (ownerKind == tapOwnerSiloVM && slot->ownerSilo != ownerSilo)
                    continue;
            }
            bySilo[slot->isMaster ? 0 : slot->silo].push_back(id);
        }
    }

    int removed = 0;
    for (auto const& [silo, ids] : bySilo) {
        if (begin(e) != tzpl_errNone) break;
        for (i64 id : ids) untap(id);
        if (go(silo) == tzpl_errNone) removed += (int)ids.size();
    }
    return removed;
}

// ---- RT-safe reads (silo's own thread; see the header for the rules). ----

bool rtTapExists(Silo* s, i64 tapID) {
    return s && s->rt_findTap(tapID) != nullptr;
}

f32 rtTapPeak(Silo* s, i64 tapID) {
    TapSlot* t = s ? s->rt_findTap(tapID) : nullptr;
    return t ? t->peak.load(std::memory_order_relaxed) : 0.f;
}

f32 rtTapRms(Silo* s, i64 tapID) {
    TapSlot* t = s ? s->rt_findTap(tapID) : nullptr;
    return t ? t->rms.load(std::memory_order_relaxed) : 0.f;
}

int rtTapChans(Silo* s, i64 tapID) {
    TapSlot* t = s ? s->rt_findTap(tapID) : nullptr;
    return t ? t->chans : 0;
}

int rtTapDrain(Silo* s, i64 tapID, f32* dst, int maxSamples) {
    TapSlot* t = s ? s->rt_findTap(tapID) : nullptr;
    if (!t) return 0;
    int n = 0;
    f32 v;
    while (n < maxSamples && t->fifo.pop(v)) dst[n++] = v;
    return n;
}

i64 allocTapID(Engine* e) {
    if (!e) return 0;
    return e->nextTapID_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Metering & monitoring
// ---------------------------------------------------------------------------

int masterChans(Engine* e) {
    return e ? e->masterMeter_.chans.load(std::memory_order_relaxed) : 0;
}

// Shared accessor shape: ch < 0 selects the across-channels summary, an
// out-of-range channel reads 0.
template <class Sum, class Per>
static f32 masterLevel(Engine* e, int ch, Sum sum, Per per) {
    if (!e) return 0.f;
    MasterMeter& m = e->masterMeter_;
    if (ch < 0) return sum(m).load(std::memory_order_relaxed);
    int n = m.chans.load(std::memory_order_relaxed);
    if (ch >= n || ch >= kMaxMasterChans) return 0.f;
    return per(m)[ch].load(std::memory_order_relaxed);
}

f32 masterPeak(Engine* e, int ch) {
    return masterLevel(e, ch,
                       [](MasterMeter& m) -> std::atomic<f32>& { return m.peakAll; },
                       [](MasterMeter& m) { return m.peak; });
}

f32 masterRms(Engine* e, int ch) {
    return masterLevel(e, ch,
                       [](MasterMeter& m) -> std::atomic<f32>& { return m.rmsAll; },
                       [](MasterMeter& m) { return m.rms; });
}

f32 masterPeakHold(Engine* e, int ch) {
    return masterLevel(e, ch,
                       [](MasterMeter& m) -> std::atomic<f32>& { return m.peakHoldAll; },
                       [](MasterMeter& m) { return m.peakHold; });
}

u32 masterClipCount(Engine* e) {
    return e ? e->masterMeter_.clipCount.load(std::memory_order_relaxed) : 0;
}

void resetMasterClip(Engine* e) {
    if (e) e->masterMeter_.clipCount.store(0, std::memory_order_relaxed);
}

static f64 msOf(u64 nanos) { return (f64)nanos / 1e6; }

void getEngineStats(Engine* e, EngineStats& out) {
    out = EngineStats{};
    if (!e) return;

    std::lock_guard<std::mutex> lck(e->nrt_lock_);

    out.audioRunning = e->audioState_ == AudioState::running;
    out.sampleRate = e->streamParams_.sampleRate;
    out.bufferFrames = e->streamParams_.bufferFrames;
    out.channels = e->streamParams_.channels;

    auto& st = e->stats_;
    out.blockCount = st.blockCount.load(std::memory_order_relaxed);
    u64 budget = st.budgetNanos.load(std::memory_order_relaxed);
    out.blockBudgetMs = msOf(budget);
    out.blockLastMs = msOf(st.lastNanos.load(std::memory_order_relaxed));
    out.blockAvgMs = msOf(st.ewmaNanos.load(std::memory_order_relaxed));
    out.blockMaxMs = msOf(st.maxNanos.load(std::memory_order_relaxed));
    if (out.blockBudgetMs > 0.) {
        out.loadPercent = 100. * out.blockAvgMs / out.blockBudgetMs;
        out.loadPeakPercent = 100. * out.blockMaxMs / out.blockBudgetMs;
    }
    out.overBudgetCount = st.overBudgetCount.load(std::memory_order_relaxed);
    out.engineDropouts = st.dropoutCount.load(std::memory_order_relaxed);
    out.badBlockSizeCount = st.badBlockSizeCount.load(std::memory_order_relaxed);
    out.rtExceptionCount = st.rtExceptionCount.load(std::memory_order_relaxed);

    if (e->backend_) {
        out.deviceTelemetry = e->backend_->hasTelemetry();
        // Report the device counter relative to the last reset. A raw value
        // BELOW the baseline means the driver's counter restarted (a device
        // change reopens it at zero), so drop the stale baseline rather than
        // wrapping into a huge number.
        u64 raw = e->backend_->deviceXruns();
        if (raw < e->deviceXrunBase_) e->deviceXrunBase_ = 0;
        out.deviceXruns = raw - e->deviceXrunBase_;
        out.deviceCpu = e->backend_->deviceCpu();
    }

    out.clipCount = e->masterMeter_.clipCount.load(std::memory_order_relaxed);
    if (e->safetyLimiter_) out.limiterGain = e->safetyLimiter_->nextGain;

    out.silos.reserve(e->silos_.size());
    for (Silo& s : e->silos_) {
        SiloStatsSnap snap;
        snap.index = s.index_;
        snap.blockCount = s.stats_.blockCount.load(std::memory_order_relaxed);
        snap.lastMs = msOf(s.stats_.lastNanos.load(std::memory_order_relaxed));
        snap.avgMs = msOf(s.stats_.ewmaNanos.load(std::memory_order_relaxed));
        snap.maxMs = msOf(s.stats_.maxNanos.load(std::memory_order_relaxed));
        snap.mixWaitMs = msOf(s.stats_.mixWaitNanos.load(std::memory_order_relaxed));
        if (out.blockBudgetMs > 0.)
            snap.loadPercent = 100. * snap.avgMs / out.blockBudgetMs;
        snap.numTaps = s.stats_.numTaps.load(std::memory_order_relaxed);
        snap.toNrtDepth = s.stats_.toNrtDepth.load(std::memory_order_relaxed);
        snap.fromNrtDepth = s.stats_.fromNrtDepth.load(std::memory_order_relaxed);
        snap.deadNodesDepth = s.stats_.deadNodesDepth.load(std::memory_order_relaxed);
        snap.hasVM = s.stats_.hasVM.load(std::memory_order_relaxed) != 0;
        snap.gcStepCount = s.stats_.gcStepCount.load(std::memory_order_relaxed);
        snap.gcCycles = s.stats_.gcCycles.load(std::memory_order_relaxed);
        snap.gcRtStepCount = s.stats_.gcRtStepCount.load(std::memory_order_relaxed);
        snap.gcRtMaxNanos = s.stats_.gcRtMaxNanos.load(std::memory_order_relaxed);
        out.silos.push_back(std::move(snap));
    }
}

void resetEngineStats(Engine* e) {
    if (!e) return;
    std::lock_guard<std::mutex> lck(e->nrt_lock_);
    // Bumps statsEpoch, which each RT writer notices on its next block and
    // restarts its running maximum from -- no CAS, no torn reset.
    e->stats_.reset();
    for (Silo& s : e->silos_) s.stats_.reset();
    e->masterMeter_.clipCount.store(0, std::memory_order_relaxed);
    // The driver's xrun counter isn't ours to zero, so re-baseline instead:
    // without this, a reset leaves the device's lifetime total in place and
    // the next stats read looks like a fresh burst of dropouts.
    if (e->backend_) e->deviceXrunBase_ = e->backend_->deviceXruns();
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

tzpl_SErr allNotesOffAll() {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new PrebuiltCmdOp(new AllNotesOffAllCmd{}));
    return tzpl_errNone;
}

tzpl_SErr clearSched() {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new PrebuiltCmdOp(new ClearSchedCmd{}));
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

// -- Sample bank commands --

tzpl_SErr replaceSampleBank(i64 nodeID, i64 bankID, tzpl_SampleBank* bank) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    tBundle.add(new PrebuiltCmdOp(new ReplaceSampleBankCmd(nodeID, bankID, bank)));
    return tzpl_errNone;
}

tzpl_SErr loadSampleBank(i64 nodeID, i64 bankID,
                         std::span<SampleBankZoneSpec const> zones) {
    if (!tBundle.engine) return tzpl_errNoActiveBundle;
    // The bank is built (files loaded, zones validated) at record time so
    // spec errors are returned synchronously and the RT swap is a pointer
    // store.
    tzpl_SErr err = tzpl_errNone;
    tzpl_SampleBank* bank = buildSampleBank(zones, &err);
    if (!bank) return err;
    tBundle.add(new PrebuiltCmdOp(new ReplaceSampleBankCmd(nodeID, bankID, bank)));
    return tzpl_errNone;
}

}
