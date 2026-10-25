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
//  tzpl_node.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//
//  The graph data model of the block engine. Every port channel is its own
//  buffer of maxBlock samples. A synth sees, per port, a table of channel
//  pointers (tzpl_InPort / tzpl_OutPort); connecting an input points its
//  table straight at the source's channel buffers, broadcasting by pointer
//  remapping. An unconnected input channel holds a value stage (constant,
//  step, ramp, impulse) that the engine renders into the input's own buffer
//  before the node runs.
//

#ifndef tzpl_node_h
#define tzpl_node_h

#include "tzpl_client_interface.hpp"
#include "tzpl_hash.hpp"
#include "tzpl_shape.hpp"

namespace engine {

// Layout-identical to tzpl_PortDef, so plugin port arrays are used in place.
struct PortInfo
{
    const char* name;
    tzpl_SignalType type;
    int hasSpec = 0;
    tzpl_ControlSpec spec{};
};
static_assert(sizeof(PortInfo) == sizeof(tzpl_PortDef));

struct BufferInfo
{
    const char* name;
    tzpl_SignalType type;
    i64 bufID;
};

struct SampleBankInfo
{
    const char* name;
    i64 bankID;
};

struct NodeDefInfo
{
    const char* name;
    tzpl_SynthFuns funs;
    int num_ins;
    int num_outs;
    PortInfo* ins;
    PortInfo* outs;
    int voiced = 0;
    int maxVoices = 0;
    const float* noteDefaults = nullptr;
    int num_buffers = 0;
    BufferInfo* buffers = nullptr;
    int num_tags = 0;
    const char** tags = nullptr;
    int num_banks = 0;
    SampleBankInfo* banks = nullptr;
    // From the optional plugin symbol "swapSampleBank".
    tzpl_SwapSampleBankFun swapSampleBank = nullptr;
};

#if DEBUG_NODES
static int numNodesCreated = 0;
static int numNodesDeleted = 0;
#endif

//=============================================================================================
#pragma mark NODE DEF

struct NodeDef {
    struct NodeDef* next_ = nullptr; // defs are stored in a hash table with separate chaining.
    NodeDefInfo info_;
    u64 hash_;

    void* dlHandle_ = nullptr;     // dlopen handle, nullptr for built-in defs
    int refCount_ = 0;             // number of live Nodes using this def
    bool superseded_ = false;      // true if a newer def with same name exists

    NodeDef(NodeDefInfo const& info)
        : info_(info), hash_(hash64(info_.name, kHashStart))
    {}

    // Index of the input port named `name`, or -1.
    int inputIndex(char const* name) const;
};

void addNodeDef(Engine* e, NodeDefInfo const& info, void* dlHandle = nullptr);
void releaseNodeDef(Engine* e, NodeDef* def);
// Newest def registered under `name` (superseded ones are behind it in the
// chain), or nullptr. Takes nrt_lock_.
NodeDef* getNodeDef(Engine* e, char const* name);

//=============================================================================================
#pragma mark PORTS

struct Node;
struct OutPort;
struct Silo;

// The value state of one input channel while it is not connected.
struct ChanStage {
    f64 value = 0.;        // the constant, or a running ramp's target
    // Ramp toward `value` (a crossfade between two constants).
    bool ramping = false;
    f64 rampFrom = 0.;
    f64 rampT0 = 0.;       // absolute fractional sample time
    i64 rampCount = 0;
    FadeCurve rampCurve = fadeLinear;
    // Pending impulse: `impulseValue` at absolute sample impulseAt only.
    i64 impulseAt = -1;
    f64 impulseValue = 0.;
    // Rendering of the current block.
    i64 stamp = -1;        // block start the fields below refer to
    int renderedTo = 0;    // samples [0, renderedTo) of the block are written
    bool touched = false;  // the buffer is not one value for this block
    bool dirty = true;     // the buffer must be refilled from sample 0
};

struct InPort {
    // Init Time: init only in NRT, read only in RT.
    Node* node_;
    int index_;
    tzpl_SignalType type_;
    int chans_ = 0;

    // Per-channel arrays (NRT-allocated, RT-written). chanPtrs_, isConst_
    // and version_ are what the synth sees through its tzpl_InPort.
    void** chanPtrs_ = nullptr;
    u8* isConst_ = nullptr;
    u32* version_ = nullptr;
    u32* ownVersion_ = nullptr;   // bumped by stage changes and (re)connects
    void** ownBufs_ = nullptr;    // the input's own buffers (value stages)
    ChanStage* stages_ = nullptr;
    int* srcMap_ = nullptr;       // source channel per channel, when connected
    // Channels whose stage needs rendering work this block or the next.
    int activeStages_ = 0;
    bool* stageActive_ = nullptr;

    OutPort* srcPort_ = nullptr;
    // doubly linked list of InPorts connected to same OutPort.
    InPort* prev_ = nullptr;
    InPort* next_ = nullptr;

    // Hidden mixer node for transparent fan-in. Non-null when 2+ sources
    // feed this port through an auto-created sum node.
    Node* mixerNode_ = nullptr;

    // ---- value stages (RT) ----
    // Write channel c's stage into its own buffer up to block offset `off`.
    void advance(Silo* s, int c, int off);
    // Step channel c to v at block offset off.
    void stepTo(Silo* s, int c, f64 v, int off);
    // Ramp channel c from its current value to v, starting at the absolute
    // fractional sample time t0 (at block offset off = first sample >= t0),
    // over `count` samples on `curve`.
    void rampTo(Silo* s, int c, f64 v, f64 t0, int off, i64 count, FadeCurve curve);
    // Value v at block offset off only; the channel's value otherwise.
    void impulse(Silo* s, int c, f64 v, int off);
    // The value channel c's stage holds at absolute time t (ramps evaluated).
    f64 valueAt(int c, f64 t) const;
    // Before the node runs: finish rendering this block, set isConst/version.
    void render(Silo* s, int n);
    // The input just became unconnected: point every channel back at its own
    // buffer and refill it.
    void restoreOwnBuffers();
    void markActive(int c) {
        if (!stageActive_[c]) { stageActive_[c] = true; ++activeStages_; }
    }
};

struct OutPort {
    // NRT: read only in RT.
    Node* node_;
    int index_;
    tzpl_SignalType type_;
    int chans_ = 0;

    // Per-channel buffers of maxBlock samples, and the producer's constness
    // report (see tzpl_OutPort).
    void** bufs_ = nullptr;
    u8* isConst_ = nullptr;
    u32* version_ = nullptr;

    InPort* dstList_ = nullptr; // list of InPorts connected to this OutPort.
};

//=============================================================================================
#pragma mark NODE

struct NodeLinks
{
    Node* prev = nullptr;
    Node* next = nullptr;
};

// A discrete event inside the current block (node splitting).
enum NodeEventKind : u8 {
    nodeEvStart,       // the node's first block starts here; output 0 before
    nodeEvFree,        // the node stops here; output 0 after
    nodeEvResetVoice,  // resetVoice(arg)
    nodeEvReset,       // reset()
};

struct NodeEvent {
    int offset;
    NodeEventKind kind;
    int arg;
};

struct NoteVoicer;

struct Node {
    // normal nodes created by user
    tzpl_SynthData* synth = nullptr;
    i64 nodeID;
    Silo* silo_ = nullptr;
    NodeLinks nrt_list;
    NodeLinks rt_list;
    Node* sorted_next = nullptr;
    u32 sortGen_ = 0;     // sort generation that last visited this node (RT)
    u32 visitMark_ = 0;   // cycle-check traversal stamp (RT)
    NodeDef* def;
    tzpl_SynthFuns funs = {0};
    std::vector<OutPort> outs;
    std::vector<InPort> ins;
    // What the synth sees (synth->ins / synth->outs point here).
    std::vector<tzpl_InPort> abiIns_;
    std::vector<tzpl_OutPort> abiOuts_;
    bool rt_active = false;
    // Which kind of hidden helper node this is, if any (see tzpl_mixer.hpp /
    // tzpl_xfader.hpp / tzpl_chanadapt.hpp). All of them have nodeID < 0, and
    // the connection code has to tell them apart.
    bool isMixer_ = false;
    bool isXFader_ = false;
    bool isAdapter_ = false;
    bool retiring_ = false; // adapter is being reclaimed (see retireAdapter)
    bool faderDone_ = false; // fader finished this block; retire after it

    // Discrete events in the current block, in time order.
    static constexpr int kMaxEvents = 64;
    NodeEvent events_[kMaxEvents];
    int numEvents_ = 0;
    i64 eventsStamp_ = -1;   // block start the events belong to
    bool started_ = false;   // has run (or been scheduled to start) a block
    bool freeing_ = false;   // a free event is pending in this block
    Node* pendingFreeNext_ = nullptr;

    // Voiced defs: the engine-side voice allocator and the `notes` input.
    NoteVoicer* voicer_ = nullptr;
    int notesPort_ = -1;

    Node(Engine* e, Silo* silo, NodeDefInfo const& inInfo); // subnode
    Node(Engine* e, Silo* silo, NodeDef* def, i64 nodeID);
    ~Node();

    void init()   { if (funs.init)   { funs.init(synth); } }
    void uninit() { if (funs.uninit) { funs.uninit(synth); } }
    void reset()  { if (funs.reset)  { funs.reset(synth); } }

    // Queue a discrete event at block offset `off` of the current block.
    void addEvent(Silo* s, int off, NodeEventKind kind, int arg = 0);
    // Run one block of n samples: render input stages, then process the
    // segments between this block's events.
    void runBlock(Silo* s, int n);
    // Zero samples [from, to) of every output.
    void zeroOutputs(int from, int to);

    int inputIndex(char const* name) const;

    tzpl_SynthData* setupSynth(Engine* e, NodeDefInfo const& info);
};

void deleteNodes(Node* node);

// Typed access to a synth's port channels, for the engine's own nodes.
template <typename T>
inline T const* inChan(tzpl_SynthData* synth, int port, int chan) {
    return static_cast<T const*>(synth->ins[port].chans[chan]);
}
template <typename T>
inline T* outChan(tzpl_SynthData* synth, int port, int chan) {
    return static_cast<T*>(synth->outs[port].chans[chan]);
}

// Write a constant into samples [from, to) of a buffer of element type elem.
void fillBuffer(void* buf, tzpl_ElemType elem, f64 v, int from, int to);

}

#endif /* tzpl_node_h */
