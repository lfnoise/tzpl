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
//  tzpl_node.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_node.hpp"
#include "tzpl_engine.hpp"
#include "tzpl_note_voicer.hpp"
#include <cstring>
#include <print>

namespace engine {

//=============================================================================================
#pragma mark BUFFERS

namespace {

constexpr usize kBufAlign = 64;

usize alignUp(usize n) { return (n + kBufAlign - 1) & ~(kBufAlign - 1); }

// One aligned allocation holding `chans` channel buffers of maxBlock
// elements each; fills `ptrs` with the channel starts. Freed with
// freeChannelBuffers(ptrs[0]).
void allocChannelBuffers(void** ptrs, int chans, int maxBlock, tzpl_ElemType elem) {
    if (chans <= 0) return;
    usize stride = alignUp((usize)maxBlock * elemSize(elem));
    u8* mem = static_cast<u8*>(std::aligned_alloc(kBufAlign, stride * (usize)chans));
    if (!mem) throw tzpl_errInternal;
    memset(mem, 0, stride * (usize)chans);
    for (int c = 0; c < chans; ++c) ptrs[c] = mem + stride * (usize)c;
}

void freeChannelBuffers(void** ptrs, int chans) {
    if (chans > 0 && ptrs) std::free(ptrs[0]);
}

template <typename T>
void fillT(void* buf, f64 v, int from, int to) {
    T* p = static_cast<T*>(buf);
    T x;
    if constexpr (std::is_integral_v<T>) x = static_cast<T>(std::llround(v));
    else x = static_cast<T>(v);
    for (int i = from; i < to; ++i) p[i] = x;
}

template <typename T>
void storeT(void* buf, int i, f64 v) {
    if constexpr (std::is_integral_v<T>) static_cast<T*>(buf)[i] = static_cast<T>(std::llround(v));
    else static_cast<T*>(buf)[i] = static_cast<T>(v);
}

void storeValue(void* buf, tzpl_ElemType elem, int i, f64 v) {
    switch (elem) {
        case tzpl_kI32: storeT<i32>(buf, i, v); break;
        case tzpl_kI64: storeT<i64>(buf, i, v); break;
        case tzpl_kF32: storeT<f32>(buf, i, v); break;
        case tzpl_kF64: storeT<f64>(buf, i, v); break;
    }
}

} // namespace

void fillBuffer(void* buf, tzpl_ElemType elem, f64 v, int from, int to) {
    switch (elem) {
        case tzpl_kI32: fillT<i32>(buf, v, from, to); break;
        case tzpl_kI64: fillT<i64>(buf, v, from, to); break;
        case tzpl_kF32: fillT<f32>(buf, v, from, to); break;
        case tzpl_kF64: fillT<f64>(buf, v, from, to); break;
    }
}

//=============================================================================================
#pragma mark INPUT STAGES

f64 InPort::valueAt(int c, f64 t) const {
    ChanStage const& st = stages_[c];
    if (!st.ramping) return st.value;
    f64 x = fadePosition(t, st.rampT0, st.rampCount);
    return fadeValue<f64>(st.rampCurve, x, st.rampFrom, st.value);
}

void InPort::advance(Silo* s, int c, int off) {
    ChanStage& st = stages_[c];
    i64 b0 = s->blockStart_;
    if (st.stamp != b0) {
        st.stamp = b0;
        st.renderedTo = 0;
        st.touched = false;
    }
    if (off <= st.renderedTo) return;
    int from = st.renderedTo;
    st.renderedTo = off;
    void* buf = ownBufs_[c];
    tzpl_ElemType elem = type_.elem;

    if (st.ramping) {
        st.touched = true;
        st.dirty = true;
        int i = from;
        for (; i < off; ++i) {
            f64 t = (f64)(b0 + i);
            f64 x = fadePosition(t, st.rampT0, st.rampCount);
            storeValue(buf, elem, i, fadeValue<f64>(st.rampCurve, x, st.rampFrom, st.value));
            if (fadeDoneAfter(t, st.rampT0, st.rampCount)) { ++i; st.ramping = false; break; }
        }
        if (i < off) fillBuffer(buf, elem, st.value, i, off);
    } else if (st.dirty) {
        fillBuffer(buf, elem, st.value, from, off);
    }

    if (st.impulseAt >= b0 + from && st.impulseAt < b0 + off) {
        storeValue(buf, elem, (int)(st.impulseAt - b0), st.impulseValue);
        st.impulseAt = -1;
        st.touched = true;
        st.dirty = true;
    }
}

void InPort::stepTo(Silo* s, int c, f64 v, int off) {
    advance(s, c, off);
    ChanStage& st = stages_[c];
    if (!st.ramping && st.value == v) return;
    st.ramping = false;
    st.value = v;
    st.dirty = true;
    // A step at offset 0 still makes the block one value, but the change has
    // to reach change detection: the version bump does that.
    if (off > 0) st.touched = true;
    ++ownVersion_[c];
    markActive(c);
}

void InPort::rampTo(Silo* s, int c, f64 v, f64 t0, int off, i64 count, FadeCurve curve) {
    advance(s, c, off);
    ChanStage& st = stages_[c];
    // A ramp started during another starts from the value rendered so far.
    st.rampFrom = valueAt(c, t0);
    st.value = v;
    st.rampT0 = t0;
    st.rampCount = count;
    st.rampCurve = curve;
    st.ramping = true;
    st.dirty = true;
    ++ownVersion_[c];
    markActive(c);
}

void InPort::impulse(Silo* s, int c, f64 v, int off) {
    advance(s, c, off);
    ChanStage& st = stages_[c];
    st.impulseAt = s->blockStart_ + off;
    st.impulseValue = v;
    ++ownVersion_[c];
    markActive(c);
}

void InPort::render(Silo* s, int n) {
    if (srcPort_) {
        OutPort* src = srcPort_;
        for (int c = 0; c < chans_; ++c) {
            int j = srcMap_[c];
            isConst_[c] = src->isConst_[j];
            version_[c] = ownVersion_[c] + src->version_[j];
        }
        return;
    }
    if (!activeStages_) return;
    int maxBlock = node_->synth->maxBlock;
    for (int c = 0; c < chans_; ++c) {
        if (!stageActive_[c]) continue;
        advance(s, c, n);
        ChanStage& st = stages_[c];
        bool constBlock = !st.touched && !st.ramping;
        // Whatever follows this block holds the stage's value, unless a ramp
        // or an impulse is still pending.
        if (!st.ramping) {
            if (constBlock) {
                // [0, n) was refilled in one piece: finish the buffer.
                if (st.dirty) fillBuffer(ownBufs_[c], type_.elem, st.value, n, maxBlock);
                st.dirty = false;
            }
        }
        isConst_[c] = constBlock ? 1 : 0;
        version_[c] = ownVersion_[c];
        if (!st.dirty && !st.ramping && st.impulseAt < 0) {
            stageActive_[c] = false;
            --activeStages_;
        }
    }
}

void InPort::restoreOwnBuffers() {
    for (int c = 0; c < chans_; ++c) {
        chanPtrs_[c] = ownBufs_[c];
        stages_[c].dirty = true;
        stages_[c].stamp = -1;
        ++ownVersion_[c];
        version_[c] = ownVersion_[c];
        isConst_[c] = 0;
        markActive(c);
    }
}

//=============================================================================================
#pragma mark NODE DEF

int NodeDef::inputIndex(char const* name) const {
    for (int i = 0; i < info_.num_ins; ++i) {
        if (info_.ins[i].name && strcmp(info_.ins[i].name, name) == 0) return i;
    }
    return -1;
}

//=============================================================================================
#pragma mark NODE METHODS

Node::Node(Engine* e, Silo* silo, NodeDefInfo const& info)
    : nodeID(-1), silo_(silo), def(nullptr)
{
#if DEBUG_NODES
    ++numNodesCreated;
#endif
    started_ = true; // hidden nodes run from the block they are spliced in
    synth = setupSynth(e, info);
    init();
}

tzpl_SynthData* Node::setupSynth(Engine* e, NodeDefInfo const& info) {
    funs = info.funs;
    int maxBlock = e->blockSize_;

    tzpl_SynthData* synth = funs.alloc(maxBlock);
    if (!synth) throw tzpl_errInternal;
    synth->engine = (tzpl_Engine*)e;
    synth->node = (tzpl_Node*)this;
    synth->funs = funs;
    synth->num_ins = info.num_ins;
    synth->num_outs = info.num_outs;
    synth->maxBlock = maxBlock;
    synth->fs = e->streamParams_.sampleRate;
    synth->sd = 1. / synth->fs;

    if (nodeID >= 0) {
        // put Node into nodeTable of silo.
        u32 bin = nodeID & kHashMask;
        Node* head = silo_->nrt_nodeTable_[bin];
        if (head) head->nrt_list.prev = this;
        nrt_list.prev = nullptr;
        nrt_list.next = head;
        silo_->nrt_nodeTable_[bin] = this;
    }

    ins.resize(info.num_ins);
    abiIns_.resize(info.num_ins);
    for (int i = 0; i < info.num_ins; ++i) {
        PortInfo const& pi = info.ins[i];
        InPort& port = ins[i];
        port.node_ = this;
        port.index_ = i;
        port.type_ = pi.type;
        int n = tzpl::chans(pi.type.shape);
        port.chans_ = n;
        port.chanPtrs_ = new void*[n];
        port.ownBufs_ = new void*[n];
        port.isConst_ = new u8[n];
        port.version_ = new u32[n];
        port.ownVersion_ = new u32[n];
        port.stages_ = new ChanStage[n];
        port.srcMap_ = new int[n];
        port.stageActive_ = new bool[n];
        allocChannelBuffers(port.ownBufs_, n, maxBlock, pi.type.elem);
        // An unconnected input holds its spec'd init value (0 without a spec).
        f64 init = pi.hasSpec ? pi.spec.init : 0.;
        for (int c = 0; c < n; ++c) {
            port.chanPtrs_[c] = port.ownBufs_[c];
            port.stages_[c].value = init;
            port.stages_[c].dirty = false;
            fillBuffer(port.ownBufs_[c], pi.type.elem, init, 0, maxBlock);
            port.isConst_[c] = 1;
            port.ownVersion_[c] = 1;
            port.version_[c] = 1;
            port.srcMap_[c] = c;
            port.stageActive_[c] = false;
        }
        abiIns_[i] = tzpl_InPort{port.chanPtrs_, port.isConst_, port.version_};
    }

    outs.resize(info.num_outs);
    abiOuts_.resize(info.num_outs);
    for (int i = 0; i < info.num_outs; ++i) {
        PortInfo const& pi = info.outs[i];
        OutPort& port = outs[i];
        port.node_ = this;
        port.index_ = i;
        port.type_ = pi.type;
        int n = tzpl::chans(pi.type.shape);
        port.chans_ = n;
        port.bufs_ = new void*[n];
        port.isConst_ = new u8[n]();
        port.version_ = new u32[n]();
        allocChannelBuffers(port.bufs_, n, maxBlock, pi.type.elem);
        abiOuts_[i] = tzpl_OutPort{port.bufs_, port.isConst_, port.version_};
    }

    synth->ins = abiIns_.data();
    synth->outs = abiOuts_.data();
    return synth;
}

Node::Node(Engine* e, Silo* silo, NodeDef* def, i64 nodeID)
    : nodeID(nodeID), silo_(silo), def(def)
{
#if DEBUG_NODES
    ++numNodesCreated;
#endif

    if (def) def->refCount_++;

    NodeDefInfo& info = def->info_;
    synth = setupSynth(e, info);

    if (info.voiced) {
        notesPort_ = def->inputIndex("notes");
        if (notesPort_ >= 0) {
            InPort const& np = ins[notesPort_];
            int rows = (int)np.type_.shape.dims[2];
            int cols = (int)np.type_.shape.dims[3];
            int maxVoices = info.maxVoices > 0 ? std::min(info.maxVoices, rows) : rows;
            voicer_ = new NoteVoicer(maxVoices, cols - 1);
            if (info.noteDefaults) {
                // Every row starts at the column defaults (gate column 0).
                voicer_->defaults.assign(info.noteDefaults, info.noteDefaults + cols);
                voicer_->defaults[0] = 0.f;
                InPort& np = ins[notesPort_];
                for (int c = 0; c < np.chans_; ++c) {
                    f64 v = voicer_->defaults[c % cols];
                    np.stages_[c].value = v;
                    fillBuffer(np.ownBufs_[c], np.type_.elem, v, 0, synth->maxBlock);
                }
            }
        }
    }

    init();
}

Node::~Node() {
    // Capture engine pointer before synth is freed.
    Engine* engine = (Engine*)synth->engine;

    // NB: do NOT call uninit() here. The generated `_free` (funs.free, below)
    // already calls `_uninit` before releasing the instance.

    // Swap out and free any installed sample buffers and banks. They are
    // owned by whoever last swapped them in; the generated `_free` does not
    // touch them. Runs on the dead-node NRT thread -- freeing here is safe.
    if (def) {
        NodeDefInfo const& info = def->info_;
        if (funs.swapBuffer) {
            for (int i = 0; i < info.num_buffers; ++i) {
                tzpl_freeBuffer(funs.swapBuffer(synth, info.buffers[i].bufID, nullptr));
            }
        }
        if (info.swapSampleBank) {
            for (int i = 0; i < info.num_banks; ++i) {
                tzpl_freeSampleBank(info.swapSampleBank(synth, info.banks[i].bankID, nullptr));
            }
        }
    }

    funs.free(synth);

    for (InPort& p : ins) {
        freeChannelBuffers(p.ownBufs_, p.chans_);
        delete[] p.chanPtrs_;
        delete[] p.ownBufs_;
        delete[] p.isConst_;
        delete[] p.version_;
        delete[] p.ownVersion_;
        delete[] p.stages_;
        delete[] p.srcMap_;
        delete[] p.stageActive_;
    }
    for (OutPort& p : outs) {
        freeChannelBuffers(p.bufs_, p.chans_);
        delete[] p.bufs_;
        delete[] p.isConst_;
        delete[] p.version_;
    }
    delete voicer_;

    // Release the def reference. May unload a superseded dylib.
    if (def) {
        releaseNodeDef(engine, def);
    }

    // remove from nrt_list
    if (nodeID >= 0) {
        int bin = nodeID & kHashMask;
        if (nrt_list.next) nrt_list.next->nrt_list.prev = nrt_list.prev;
        if (nrt_list.prev) nrt_list.prev->nrt_list.next = nrt_list.next;
        else silo_->nrt_nodeTable_[bin] = nrt_list.next;
    }
#if DEBUG_NODES
    ++numNodesDeleted;
#endif
}

int Node::inputIndex(char const* name) const {
    if (def) return def->inputIndex(name);
    return -1;
}

void Node::addEvent(Silo* s, int off, NodeEventKind kind, int arg) {
    if (eventsStamp_ != s->blockStart_) {
        eventsStamp_ = s->blockStart_;
        numEvents_ = 0;
    }
    if (numEvents_ >= kMaxEvents) {
        ++s->droppedNodeEvents_;
        return;
    }
    // Events arrive in time order; never let one land before its predecessor.
    if (numEvents_ > 0 && off < events_[numEvents_ - 1].offset)
        off = events_[numEvents_ - 1].offset;
    events_[numEvents_++] = NodeEvent{off, kind, arg};
}

void Node::zeroOutputs(int from, int to) {
    if (to <= from) return;
    for (OutPort& p : outs) {
        for (int c = 0; c < p.chans_; ++c) fillBuffer(p.bufs_[c], p.type_.elem, 0., from, to);
    }
}

void Node::runBlock(Silo* s, int n) {
    for (InPort& p : ins) p.render(s, n);

    if (eventsStamp_ != s->blockStart_ || numEvents_ == 0) {
        funs.process(synth, 0, n);
        return;
    }
    beginSpans(s, n);
    runSpan(0, n);
    endSpans();
}

void Node::beginSpans(Silo* s, int n) {
    spanEv_ = 0;
    spanN_ = n;
    spanEvents_ = eventsStamp_ == s->blockStart_ && numEvents_ > 0;
    // A node starting mid-block outputs 0 before its start; a node freed
    // mid-block outputs 0 after it.
    spanRunning_ = true;
    if (spanEvents_) {
        for (int k = 0; k < numEvents_; ++k) {
            if (events_[k].kind == nodeEvStart) { spanRunning_ = false; break; }
        }
    }
}

// Split [lo, hi) at each event in it. An event at the block's end (offset
// n) belongs to the last span.
void Node::runSpan(int lo, int hi) {
    int pos = lo;
    while (spanEvents_ && spanEv_ < numEvents_) {
        NodeEvent const& ev = events_[spanEv_];
        int off = std::min(ev.offset, spanN_);
        if (!(off < hi || (hi == spanN_ && off <= hi))) break;
        ++spanEv_;
        switch (ev.kind) {
            case nodeEvStart:
                if (!spanRunning_) {
                    zeroOutputs(pos, off);
                    pos = off;
                    spanRunning_ = true;
                }
                break;
            case nodeEvFree:
                if (spanRunning_ && off > pos) funs.process(synth, pos, off - pos);
                if (spanRunning_) pos = off;
                zeroOutputs(pos, spanN_);
                pos = hi;
                spanRunning_ = false;
                break;
            case nodeEvResetVoice:
            case nodeEvReset:
                if (spanRunning_ && off > pos) {
                    funs.process(synth, pos, off - pos);
                    pos = off;
                }
                if (ev.kind == nodeEvReset) reset();
                else if (funs.resetVoice) funs.resetVoice(synth, ev.arg, off);
                break;
        }
    }
    if (spanRunning_ && pos < hi) funs.process(synth, pos, hi - pos);
    else if (!spanRunning_ && pos < hi) zeroOutputs(pos, hi);
}

void deleteNodes(Node* node) {
    while (node) {
        Node* next = node->rt_list.next;
        delete node;
        node = next;
    }
}

}
