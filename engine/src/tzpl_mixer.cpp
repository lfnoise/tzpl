// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//
//  tzpl_mixer.cpp
//  audio engine
//
//  Hidden mixer (sum) node for transparent fan-in.
//

#include "tzpl_mixer.hpp"
#include "tzpl_engine.hpp"

namespace engine {

// ---------------------------------------------------------------------------
// MixerData -- extends tzpl_SynthData with channel count
// ---------------------------------------------------------------------------

struct MixerData : tzpl_SynthData {
    int numChannels;
};

// ---------------------------------------------------------------------------
// SynthFuns callbacks
// ---------------------------------------------------------------------------

static tzpl_SynthData* Mixer_alloc() {
    return (tzpl_SynthData*)new MixerData();
}

static tzpl_SErr Mixer_free(tzpl_SynthData* synth) {
    delete (MixerData*)synth;
    return tzpl_errNone;
}

static tzpl_SErr Mixer_init(tzpl_SynthData* synth) {
    return tzpl_errNone;
}

static tzpl_SErr Mixer_uninit(tzpl_SynthData* synth) {
    return tzpl_errNone;
}

// Sum all inputs to a single output.
// Disconnected inputs point to zeroed fallback buffers, so it is always
// safe to sum every slot without checking srcPort_.
template <typename T>
static void Mixer_processAudio(tzpl_SynthData* synth) {
    MixerData* m = (MixerData*)synth;
    T* out = getOut<T>(synth, 0);
    int nc = m->numChannels;
    int ni = synth->num_ins;

    // Zero the output
    for (int c = 0; c < nc; ++c) out[c] = T(0);

    // Accumulate all inputs
    for (int i = 0; i < ni; ++i) {
        T const* in = getIn<T>(synth, i);
        for (int c = 0; c < nc; ++c) {
            out[c] += in[c];
        }
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

Node* newMixerNode(Engine* e, Silo* silo, tzpl_SignalType type, int capacity) {
    tzpl_SynthFuns funs = {0};
    funs.alloc  = Mixer_alloc;
    funs.free   = Mixer_free;
    funs.init   = Mixer_init;
    funs.uninit = Mixer_uninit;

    switch (type.elem) {
        case tzpl_kF32: funs.processAudio = Mixer_processAudio<f32>; break;
        case tzpl_kF64: funs.processAudio = Mixer_processAudio<f64>; break;
        case tzpl_kI32: funs.processAudio = Mixer_processAudio<i32>; break;
        case tzpl_kI64: funs.processAudio = Mixer_processAudio<i64>; break;
    }

    NodeDefInfo info;
    memset(&info, 0, sizeof(info));

    info.name = "mixer";
    info.num_ins = capacity;
    info.num_outs = 1;
    info.funs = funs;

    // All input ports share the same type as the destination
    info.ins = (PortInfo*)calloc(capacity, sizeof(PortInfo));
    for (int i = 0; i < capacity; ++i) {
        info.ins[i] = PortInfo{"in", type};
    }

    info.outs = (PortInfo*)calloc(1, sizeof(PortInfo));
    info.outs[0] = PortInfo{"out", type};

    Node* mixer = new Node(e, silo, info);
    mixer->isMixer_ = true;

    free(info.ins);
    free(info.outs);

    MixerData* md = (MixerData*)mixer->synth;
    md->numChannels = type.chans;

    return mixer;
}

// ---------------------------------------------------------------------------
// Chain walking
// ---------------------------------------------------------------------------

// The next mixer down the chain (the one whose output this mixer sums), or
// null at the end of the chain. Only mixers ever feed mixers, so the source
// node's isMixer_ flag identifies a chain link.
static Node* mixerLink(Node* mixer) {
    for (auto& in : mixer->ins) {
        if (in.srcPort_ && in.srcPort_->node_->isMixer_) return in.srcPort_->node_;
    }
    return nullptr;
}

// True if a crossfader is still fading into one of the chain's slots. The
// crossfader reads the slot's fallback buffer, so the chain must outlive it.
static bool hasPendingFade(Node* head) {
    for (Node* m = head; m; m = mixerLink(m)) {
        for (auto& in : m->ins) {
            if (in.srcPort_ && in.srcPort_->node_->isXFader_) return true;
        }
    }
    return false;
}

MixerSlot findFreeMixerSlot(Node* head) {
    for (Node* m = head; m; m = mixerLink(m)) {
        for (int i = 0; i < (int)m->ins.size(); ++i) {
            if (!m->ins[i].srcPort_) return {m, i};
        }
    }
    return {};
}

MixerSlot findMixerSlot(Node* head, OutPort* src) {
    for (Node* m = head; m; m = mixerLink(m)) {
        for (int i = 0; i < (int)m->ins.size(); ++i) {
            if (m->ins[i].srcPort_ == src) return {m, i};
        }
    }
    return {};
}

int countActiveMixerInputs(Node* head) {
    int count = 0;
    for (Node* m = head; m; m = mixerLink(m)) {
        for (auto& in : m->ins) {
            if (in.srcPort_ && !in.srcPort_->node_->isMixer_) ++count;
        }
    }
    return count;
}

int collectMixerSources(Node* head, OutPort** out, int max) {
    int count = 0;
    for (Node* m = head; m; m = mixerLink(m)) {
        for (auto& in : m->ins) {
            if (!in.srcPort_ || in.srcPort_->node_->isMixer_) continue;
            if (count < max) out[count++] = in.srcPort_;
        }
    }
    return count;
}

InPort* mixerChainOwner(Node* hidden) {
    // Hidden nodes have a single output feeding a single destination, so
    // walking up is a straight line. The bound is paranoia, not topology.
    for (int depth = 0; hidden && hidden->nodeID < 0 && depth < 64; ++depth) {
        if (hidden->outs.empty()) return nullptr;
        InPort* dst = hidden->outs[0].dstList_;
        if (!dst) return nullptr;
        if (dst->mixerNode_) return dst;
        hidden = dst->node_;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Splice / Chain / Add / Remove / Collapse
// ---------------------------------------------------------------------------

tzpl_SErr spliceMixer(Silo* s, Node* mixer, OutPort* newSrc, InPort* dst) {
    // dst currently has a direct connection to some source
    OutPort* oldSrc = dst->srcPort_;

    // Wire: oldSrc -> mixer.in[0], newSrc -> mixer.in[1], mixer.out -> dst
    if (oldSrc) {
        tzpl_SErr err = s->connect(oldSrc, &mixer->ins[0]);
        if (err != tzpl_errNone) return err;
    }

    tzpl_SErr err = s->connect(newSrc, &mixer->ins[1]);
    if (err != tzpl_errNone) return err;

    err = s->connect(&mixer->outs[0], dst);
    if (err != tzpl_errNone) return err;

    dst->mixerNode_ = mixer;
    return tzpl_errNone;
}

tzpl_SErr chainMixer(Silo* s, InPort* dst, Node* newHead) {
    Node* oldHead = dst->mixerNode_;
    if (!oldHead) return tzpl_errInternal;

    // The old head's output now feeds the new head as well as dst; the
    // second connect unlinks it from dst. Nothing already connected moves,
    // so any crossfader running into the old chain keeps its buffers.
    tzpl_SErr err = s->connect(&oldHead->outs[0], &newHead->ins[0]);
    if (err != tzpl_errNone) return err;

    err = s->connect(&newHead->outs[0], dst);
    if (err != tzpl_errNone) {
        s->disconnect(&newHead->ins[0]); // leave the old chain as it was
        return err;
    }

    dst->mixerNode_ = newHead;
    return tzpl_errNone;
}

tzpl_SErr addToMixer(Silo* s, Node* mixer, OutPort* newSrc, int slot) {
    return s->connect(newSrc, &mixer->ins[slot]);
}

bool removeFromMixer(Silo* s, Node* head, OutPort* src) {
    MixerSlot found = findMixerSlot(head, src);
    if (!found) return false;
    s->disconnect(&found.mixer->ins[found.slot]);
    return true;
}

// Unlink and kill every mixer in the chain. Returns the one remaining real
// source, if any (the caller decides what to do with it).
static OutPort* teardownChain(Silo* s, InPort* dst, bool freeFaders) {
    OutPort* remaining = nullptr;
    Node* m = dst->mixerNode_;
    while (m) {
        Node* next = mixerLink(m); // before disconnecting clears the link
        for (auto& in : m->ins) {
            Node* src = in.srcPort_ ? in.srcPort_->node_ : nullptr;
            if (!src || src->isMixer_) continue;
            if (freeFaders && src->isXFader_) {
                // A crossfader fading into this chain: once the chain is
                // gone nothing reaches it, so it would never self-remove.
                s->disconnectNode(src);
                s->pushDeadNode(src);
                continue;
            }
            if (!remaining) remaining = in.srcPort_;
        }
        s->disconnectNode(m);
        s->pushDeadNode(m);
        m = next;
    }
    dst->mixerNode_ = nullptr;
    return remaining;
}

void collapseMixer(Silo* s, InPort* dst) {
    Node* head = dst->mixerNode_;
    if (!head) return;
    // Deferred while a fade is in flight: the crossfader reads a slot's
    // fallback buffer, which dies with the mixer that owns it. Leaving the
    // chain in place is harmless -- a lone source just sums with zeroes.
    if (hasPendingFade(head)) return;

    OutPort* remaining = teardownChain(s, dst, /*freeFaders=*/false);

    // Restore direct connection (or leave disconnected if no source remains)
    if (remaining) {
        s->connect(remaining, dst);
    }
}

void freeMixerChain(Silo* s, InPort* dst) {
    if (!dst->mixerNode_) return;
    teardownChain(s, dst, /*freeFaders=*/true);
}

} // namespace engine
