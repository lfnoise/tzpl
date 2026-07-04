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
//  tzpl_command_subclasses.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_command_subclasses_h
#define tzpl_command_subclasses_h

#include "tzpl_silo.hpp"
#include "tzpl_engine.hpp"
#include "tzpl_xfader.hpp"
#include "tzpl_mixer.hpp"
#include "tzpl_audio_file.hpp"

namespace engine {

//=============================================================================================
#pragma mark COMMAND SUBCLASSES

// NB: several commands own pre-allocated objects (nodes, mixers, xfaders,
// buffers) between construction and execution. A destructor that sees
// stage_ == 0 is destroying a command that never ran -- an aborted or
// abandoned bundle -- and must free what the command owns. Once doRT has
// run (stage_ > 0), ownership has passed to the RT graph / dead-node path
// and the guards are no-ops.

struct AddNodeCmd : Command
{
    Node* node_;

    AddNodeCmd(Node* node) : node_(node) {}

    // Never ran: the node was allocated and inserted into the silo's NRT
    // table at bundle submit; ~Node removes it again. Caller holds nrt_lock_.
    ~AddNodeCmd() override { if (stage_ == 0) delete node_; }

    void doRT(Silo* s) override {
        err_ = s->addNode(node_);
    }
};

struct RemoveNodeCmd : Command
{
    i64 nodeID_;
    Node* node_ = nullptr;
    // Mixer nodes that need collapsing after removal (collected pre-disconnect)
    static constexpr int kMaxMixerCollapse = 8;
    InPort* mixerDsts_[kMaxMixerCollapse] = {};
    int numMixerDsts_ = 0;

    RemoveNodeCmd(i64 nodeID) : nodeID_(nodeID) {}

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) return;

        // Before disconnecting, find any mixer inputs this node's outputs feed.
        // After removeNode, the dstList_ links will be gone.
        for (auto& out : node->outs) {
            for (InPort* dst = out.dstList_; dst; dst = dst->next_) {
                // If this dst belongs to a mixer node, record the ultimate
                // destination InPort (the one with mixerNode_ set).
                Node* dstNode = dst->node_;
                if (dstNode && dstNode->nodeID < 0) {
                    // This is a hidden sub-node (mixer or xfader).
                    // Find the InPort that owns this mixer.
                    for (auto& mixerOut : dstNode->outs) {
                        for (InPort* finalDst = mixerOut.dstList_; finalDst; finalDst = finalDst->next_) {
                            if (finalDst->mixerNode_ == dstNode && numMixerDsts_ < kMaxMixerCollapse) {
                                mixerDsts_[numMixerDsts_++] = finalDst;
                            }
                        }
                    }
                }
            }
        }

        err_ = s->removeNode(node);
        node_ = node;

        // After removal, check if any collected mixers should collapse
        for (int i = 0; i < numMixerDsts_; ++i) {
            InPort* dst = mixerDsts_[i];
            if (dst->mixerNode_ && countActiveMixerInputs(dst->mixerNode_) <= 1) {
                collapseMixer(s, dst);
            }
        }
    }
    bool doNRT(Silo* s) override {
        if (node_) delete node_;
        return true;
    }
};

struct RemoveAllNodesCmd : Command
{
    Node* nodes_ = nullptr;
    
    void doRT(Silo* s) override {
        nodes_ = s->removeAllNodes();
    }
    bool doNRT(Silo* s) override {
        deleteNodes(nodes_); //FIXME? deleteNodes operates on the RT list.
        return true;
    }
};

struct ConnectCmd : Command
{
    PortAddr src_;
    PortAddr dst_;
    Node* xfaderNode_;
    Node* mixerNode_;      // pre-allocated mixer (null if not needed)
    int mixerSlot_;         // which mixer input slot to use (-1 if N/A)
    FadeCurve curve_;

    ConnectCmd(PortAddr src, PortAddr dst, Node* xfaderNode, FadeCurve curve,
               Node* mixerNode = nullptr, int mixerSlot = -1)
        : src_(src), dst_(dst), xfaderNode_(xfaderNode),
          mixerNode_(mixerNode), mixerSlot_(mixerSlot), curve_(curve)
    {}

    // Never ran: hidden nodes (nodeID -1) are in no table; plain delete.
    ~ConnectCmd() override {
        if (stage_ == 0) {
            delete xfaderNode_;
            delete mixerNode_;
        }
    }

    void doRT(Silo* s) override {
        OutPort* src;
        err_ = s->rt_getOutPort(src_, src);
        if (err_ != tzpl_errNone) return;

        InPort* dst;
        s->rt_getInPort(dst_, dst);
        if (err_ != tzpl_errNone) return;

        // RT decides based on actual connection state whether fan-in is needed.
        bool needsFanIn = dst->srcPort_ != nullptr || dst->mixerNode_ != nullptr;

        if (needsFanIn && dst->mixerNode_) {
            // Already has a mixer: add source to an existing slot.
            int slot = findFreeMixerSlot(dst->mixerNode_);
            if (slot < 0) {
                // Mixer full. Use the pre-allocated mixer as a replacement
                // with double capacity (migrate existing connections).
                // For now, just use a free slot on the pre-allocated mixer.
                // TODO: implement mixer growth at RT
                err_ = tzpl_errNone;
                return;
            }
            if (xfaderNode_) {
                err_ = setupXFader(s, xfaderNode_, src, &dst->mixerNode_->ins[slot], curve_, nullptr);
            } else {
                err_ = addToMixer(s, dst->mixerNode_, src, slot);
            }
            // Discard the pre-allocated mixer (not needed)
            if (mixerNode_) s->pushDeadNode(mixerNode_);
        } else if (needsFanIn && mixerNode_) {
            // Transition from 1 source to 2: splice in the pre-allocated mixer.
            if (xfaderNode_) {
                OutPort* oldSrc = dst->srcPort_;
                err_ = s->connect(&mixerNode_->outs[0], dst);
                if (err_ != tzpl_errNone) return;
                if (oldSrc) s->connect(oldSrc, &mixerNode_->ins[0]);
                dst->mixerNode_ = mixerNode_;
                err_ = setupXFader(s, xfaderNode_, src, &mixerNode_->ins[1], curve_, nullptr);
            } else {
                err_ = spliceMixer(s, mixerNode_, src, dst);
            }
        } else {
            // No fan-in: first connection (or destination was empty).
            if (xfaderNode_) {
                err_ = setupXFader(s, xfaderNode_, src, dst, curve_, nullptr);
            } else {
                err_ = s->connect(src, dst);
            }
            // Discard the pre-allocated mixer (not needed)
            if (mixerNode_) s->pushDeadNode(mixerNode_);
        }
    }
};

struct ReconnectOutputCmd : Command
{
    PortAddr oldSrc_;
    PortAddr newSrc_;
    Node* xfaderNode_;
    FadeCurve curve_;
    
    ReconnectOutputCmd(PortAddr oldSrc, PortAddr newSrc, Node* xfaderNode, FadeCurve curve)
        : oldSrc_(oldSrc), newSrc_(newSrc), xfaderNode_(xfaderNode), curve_(curve)
    {}

    ~ReconnectOutputCmd() override { if (stage_ == 0) delete xfaderNode_; }
    
    void doRT(Silo* s) override {
        OutPort* oldSrc;
        err_ = s->rt_getOutPort(oldSrc_, oldSrc);
        if (err_ != tzpl_errNone) return;
        
        OutPort* newSrc;
        err_ = s->rt_getOutPort(newSrc_, newSrc);
        if (err_ != tzpl_errNone) return;

        if (xfaderNode_) {
            err_ = setupXFader(s, xfaderNode_, oldSrc, newSrc, curve_);
        } else {
            err_ = s->reconnectOutput(oldSrc, newSrc);
        }
        //printf("connect RT err %d\n", err_);
    }
};

struct DisconnectInputCmd : Command
{
    PortAddr dst_;
    Node* xfaderNode_;
    FadeCurve curve_;

    DisconnectInputCmd(PortAddr dst, Node* xfaderNode, FadeCurve curve)
        : dst_(dst), xfaderNode_(xfaderNode), curve_(curve)
    {}

    ~DisconnectInputCmd() override { if (stage_ == 0) delete xfaderNode_; }

    void doRT(Silo* s) override {
        InPort* dst;
        err_ = s->rt_getInPort(dst_, dst);
        if (err_ != tzpl_errNone) return;

        // Tear down hidden mixer if present
        if (dst->mixerNode_) {
            s->disconnectNode(dst->mixerNode_);
            s->pushDeadNode(dst->mixerNode_);
            dst->mixerNode_ = nullptr;
        }

        if (xfaderNode_) {
            err_ = setupXFader(s, xfaderNode_, nullptr, dst, curve_, dst->dataBuffer_);
        } else {
            err_ = s->disconnect(dst);
        }
    }
};

struct DisconnectSourceCmd : Command
{
    PortAddr src_;
    PortAddr dst_;
    Node* xfaderNode_;
    FadeCurve curve_;

    DisconnectSourceCmd(PortAddr src, PortAddr dst, Node* xfaderNode, FadeCurve curve)
        : src_(src), dst_(dst), xfaderNode_(xfaderNode), curve_(curve)
    {}

    ~DisconnectSourceCmd() override { if (stage_ == 0) delete xfaderNode_; }

    void doRT(Silo* s) override {
        OutPort* src;
        err_ = s->rt_getOutPort(src_, src);
        if (err_ != tzpl_errNone) return;

        InPort* dst;
        err_ = s->rt_getInPort(dst_, dst);
        if (err_ != tzpl_errNone) return;

        if (!dst->mixerNode_) {
            // No mixer -- just disconnect the direct connection
            if (xfaderNode_) {
                err_ = setupXFader(s, xfaderNode_, nullptr, dst, curve_, dst->dataBuffer_);
            } else {
                err_ = s->disconnect(dst);
            }
            return;
        }

        // Has a mixer -- remove this specific source from it
        Node* mixer = dst->mixerNode_;
        if (xfaderNode_) {
            // Find the mixer input slot connected to src and fade it out
            for (int i = 0; i < (int)mixer->ins.size(); ++i) {
                if (mixer->ins[i].srcPort_ == src) {
                    err_ = setupXFader(s, xfaderNode_, nullptr, &mixer->ins[i],
                                       curve_, mixer->ins[i].dataBuffer_);
                    break;
                }
            }
            // Mixer collapse after xfade is deferred (dead slot sums zero)
        } else {
            removeFromMixer(s, mixer, src);
            if (countActiveMixerInputs(mixer) <= 1) {
                collapseMixer(s, dst);
            }
        }
    }
};

struct DisconnectOutputCmd : Command
{
    PortAddr src_;

    DisconnectOutputCmd(PortAddr src)
        : src_(src)
    {}

    void doRT(Silo* s) override {
        OutPort* src;
        s->rt_getOutPort(src_, src);
        if (!src) return;

        s->disconnect(src);
    }
};

struct DisconnectNodeCmd : Command
{
    i64 nodeID_;

    DisconnectNodeCmd(i64 nodeID)
        : nodeID_(nodeID)
    {}

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        s->disconnectNode(node);
    }
};

template <class T>
struct SetInputCmd : Command
{
    PortAddr dst_;
    std::vector<T> values_;
    Node* xfaderNode_;
    FadeCurve curve_;

    SetInputCmd(PortAddr dst, usize numValues,  T* values,
            Node* xfaderNode = nullptr, FadeCurve curve = fadeLinear)
        : dst_(dst), values_(numValues), xfaderNode_(xfaderNode), curve_(curve)
    {
        memcpy(&values_[0], values, numValues*sizeof(T));
    }

    ~SetInputCmd() override { if (stage_ == 0) delete xfaderNode_; }

    void doRT(Silo* s) override {
        InPort* dst;
        err_ = s->rt_getInPort(dst_, dst);
        if (err_ != tzpl_errNone) return;
        
        if (xfaderNode_) {
            err_ = setupXFader(s, xfaderNode_, nullptr, dst, curve_, &values_[0]);
        }
        s->setInput(dst, int(values_.size()), &values_[0]);
    }
};

template <class T>
struct SetControlCmd : Command
{
    i64 nodeID_;
    i64 controlID_;
    std::vector<T> values_;

    SetControlCmd(i64 nodeID, i64 controlID, int numValues,  T* values)
        : nodeID_(nodeID), controlID_(controlID), values_(numValues)
    {
        memcpy(&values_[0], values, numValues*sizeof(T));
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (node) {
            int len = int(values_.size());
            s->setControl(node, controlID_, len, &values_[0]);
        }
    }
};

struct NoteOnCmd : Command
{
    i64 nodeID_;
    int noteID_;
    std::vector<f32> values_;

    NoteOnCmd(i64 nodeID, int noteID, int numValues, f32* values)
        : nodeID_(nodeID), noteID_(noteID), values_(numValues)
    {
        if (values)
            memcpy(&values_[0], values, numValues*sizeof(f32));
    }

    bool isNoteOn() const override { return true; } // for debug

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
            printf("NoteOnCmd node not found.\n");
        } else {
            i64 now = s->sampleTime_;
            int len = int(values_.size());
            node->funs.noteOn(node->synth, now, noteID_, len, &values_[0]);
        }
    }
};

struct NoteOffCmd : Command
{
    i64 nodeID_;
    int noteID_;


    NoteOffCmd(i64 nodeID, int noteID)
        : nodeID_(nodeID), noteID_(noteID)
    {}

    bool isNoteOff() const override { return true; } // for debug

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
            printf("NoteOffCmd node not found\n");
        } else {
            i64 now = s->sampleTime_;
            node->funs.noteOff(node->synth, now, noteID_);
        }
    }
};

struct AllNotesOffCmd : Command {
    i64 nodeID_;

    AllNotesOffCmd(i64 nodeID)
        : nodeID_(nodeID)
    {}

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
        } else {
            i64 now = s->sampleTime_;
            node->funs.allNotesOff(node->synth, now);
        }
    }
};

struct NoteSetParamRangeCmd : Command {
    i64 nodeID_;
    int noteID_;
    int first_;
    std::vector<f32> values_;

    NoteSetParamRangeCmd(i64 nodeID, int noteID, int first, int length,  f32* values)
        : nodeID_(nodeID), noteID_(noteID), first_(first), values_(length)
    {
        memcpy(&values_[0], values, length*sizeof(f32));
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (node) {
            int len = int(values_.size());
            node->funs.noteSetParamRange(node->synth, noteID_, first_, len, &values_[0]);
        }
    }
};

struct NoteSetParamsCmd : Command {
    i64 nodeID_;
    int noteID_;
    std::vector<tzpl_ParamPair> values_;

    NoteSetParamsCmd(i64 nodeID, int noteID, int length, tzpl_ParamPair* values)
        : nodeID_(nodeID), noteID_(noteID), values_(length)
    {
        memcpy(&values_[0], values, length*sizeof(tzpl_ParamPair));
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (node) {
            int len = int(values_.size());
            node->funs.noteSetParams(node->synth, noteID_, len, &values_[0]);
        } else {
            err_ = tzpl_errNodeNotFound;
        }      
    }
};

struct MasterGainCmd : Command
{
    f32 gain_;
};

struct ChannelOffsetCmd : Command
{
    i32 offset_;

    ChannelOffsetCmd(i32 offset) : offset_(offset) {}

    void doRT(Silo* s) override {
        s->channelOffset_ = offset_;
    }
};

// ---------------------------------------------------------------------------
// Buffer commands
// ---------------------------------------------------------------------------

struct ResizeBufferCmd : Command {
    i64 nodeID_;
    i64 bufID_;
    tzpl_Buffer* newBuf_;
    tzpl_Buffer* oldBuf_ = nullptr;

    ResizeBufferCmd(i64 nodeID, i64 bufID, int numChannels, i64 length)
        : nodeID_(nodeID), bufID_(bufID)
    {
        newBuf_ = tzpl_createBuffer(numChannels, length);
    }

    ~ResizeBufferCmd() override { if (stage_ == 0) tzpl_freeBuffer(newBuf_); }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node || !node->funs.swapBuffer) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        oldBuf_ = node->funs.swapBuffer(node->synth, bufID_, newBuf_);
    }

    bool doNRT(Silo* s) override {
        tzpl_freeBuffer(oldBuf_);
        return true;
    }
};

struct ReplaceBufferCmd : Command {
    i64 nodeID_;
    i64 bufID_;
    tzpl_Buffer* newBuf_;
    tzpl_Buffer* oldBuf_ = nullptr;

    ReplaceBufferCmd(i64 nodeID, i64 bufID, tzpl_Buffer* buffer)
        : nodeID_(nodeID), bufID_(bufID), newBuf_(buffer) {}

    ~ReplaceBufferCmd() override { if (stage_ == 0) tzpl_freeBuffer(newBuf_); }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node || !node->funs.swapBuffer) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        oldBuf_ = node->funs.swapBuffer(node->synth, bufID_, newBuf_);
    }

    bool doNRT(Silo* s) override {
        tzpl_freeBuffer(oldBuf_);
        return true;
    }
};

struct LoadBufferCmd : Command {
    i64 nodeID_;
    i64 bufID_;
    tzpl_Buffer* newBuf_;
    tzpl_Buffer* oldBuf_ = nullptr;

    LoadBufferCmd(i64 nodeID, i64 bufID, const char* path,
                  int channelOffset, i64 frameOffset, i64 numFrames)
        : nodeID_(nodeID), bufID_(bufID)
    {
        newBuf_ = tzpl_loadAudioFile(path, channelOffset, frameOffset, numFrames);
    }

    ~LoadBufferCmd() override { if (stage_ == 0) tzpl_freeBuffer(newBuf_); }

    void doRT(Silo* s) override {
        if (!newBuf_) {
            err_ = tzpl_errInternal;
            return;
        }
        Node* node = s->rt_getNode(nodeID_);
        if (!node || !node->funs.swapBuffer) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        oldBuf_ = node->funs.swapBuffer(node->synth, bufID_, newBuf_);
    }

    bool doNRT(Silo* s) override {
        tzpl_freeBuffer(oldBuf_);
        return true;
    }
};

//=============================================================================================
#pragma mark TEMPO CLOCK COMMANDS

//=============================================================================================
#pragma mark TAP COMMANDS

// Install a signal tap on a node outlet. The TapSlot was created at bundle
// submit and lives in the Engine's tap registry; this command hands the RT
// thread a raw pointer to install in the silo's tap table.
struct TapOutletCmd : Command
{
    Engine* engine_;
    i64 nodeID_;
    int outlet_;
    i64 tapID_;
    TapSlot* slot_;  // owned by engine_->taps_

    TapOutletCmd(Engine* e, i64 nodeID, int outlet, i64 tapID, TapSlot* slot)
        : engine_(e), nodeID_(nodeID), outlet_(outlet), tapID_(tapID), slot_(slot)
    {}

    // Never ran: the bundle was aborted after the registry entry was created
    // at submit; remove it again. Caller holds nrt_lock_.
    ~TapOutletCmd() override {
        if (stage_ == 0) engine_->taps_.erase(tapID_);
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) { err_ = tzpl_errNodeNotFound; return; }
        err_ = s->installTap(slot_, node, outlet_, tapID_);
    }

    bool doNRT(Silo* s) override {
        // Install failed (node gone / tap table full): free the registry
        // entry so the tapID reads as silence and can be reused.
        // nrt_lock_ is held by the NRT drain loop.
        if (err_ != tzpl_errNone) {
            s->engine_->taps_.erase(tapID_);
        }
        return true;
    }
};

struct UntapCmd : Command
{
    i64 tapID_;

    UntapCmd(i64 tapID) : tapID_(tapID) {}

    void doRT(Silo* s) override {
        s->removeTap(tapID_);
    }

    bool doNRT(Silo* s) override {
        // The RT thread no longer references the slot; free it.
        // nrt_lock_ is held by the NRT drain loop.
        s->engine_->taps_.erase(tapID_);
        return true;
    }
};

// Immediate tempo set on a clock slot. Broadcast to every silo; applied at the
// receiving silo's current sample so all silos produce an identical ramp.
struct SetClockTempoCmd : Command
{
    f64 bps_; // beats per second

    SetClockTempoCmd(int clock, f64 bps) : bps_(bps) {
        clock_ = clock;
        schedPolicy_ = schedImmediate;
    }

    void doRT(Silo* s) override {
        if (clock_ >= 0 && clock_ < (int)s->tempoClocks_.size()) {
            s->tempoClocks_[clock_].setTempoNow(bps_, s->sampleTime_);
        }
    }
};

// Scheduled tempo ramp on a clock slot, applied when the clock reaches atBeat.
// Broadcast to every silo; beat-anchored so all silos stay identical.
struct SchedClockTempoRampCmd : Command
{
    f64 targetBPS_;
    f64 rampBeats_;

    SchedClockTempoRampCmd(int clock, f64 atBeat, f64 targetBPS, f64 rampBeats)
        : targetBPS_(targetBPS), rampBeats_(rampBeats)
    {
        clock_ = clock;
        beatTime_ = atBeat;
        schedPolicy_ = schedBetterLateThanNever;
    }

    void doRT(Silo* s) override {
        if (clock_ >= 0 && clock_ < (int)s->tempoClocks_.size()) {
            s->tempoClocks_[clock_].applyRamp(beatTime_, targetBPS_, rampBeats_);
        }
    }
};

}

#endif /* tzpl_command_subclasses_h */
