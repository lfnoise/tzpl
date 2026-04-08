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

    free(info.ins);
    free(info.outs);

    MixerData* md = (MixerData*)mixer->synth;
    md->numChannels = type.chans;

    return mixer;
}

// ---------------------------------------------------------------------------
// Splice / Add / Remove / Collapse
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

tzpl_SErr addToMixer(Silo* s, Node* mixer, OutPort* newSrc, int slot) {
    return s->connect(newSrc, &mixer->ins[slot]);
}

int removeFromMixer(Silo* s, Node* mixer, OutPort* src) {
    for (int i = 0; i < (int)mixer->ins.size(); ++i) {
        if (mixer->ins[i].srcPort_ == src) {
            s->disconnect(&mixer->ins[i]);
            return i;
        }
    }
    return -1;
}

void collapseMixer(Silo* s, InPort* dst) {
    Node* mixer = dst->mixerNode_;
    if (!mixer) return;

    // Find the one remaining active source
    OutPort* remaining = nullptr;
    for (auto& in : mixer->ins) {
        if (in.srcPort_) {
            remaining = in.srcPort_;
            break;
        }
    }

    // Disconnect mixer from the graph
    s->disconnectNode(mixer);

    // Restore direct connection (or leave disconnected if no source remains)
    if (remaining) {
        s->connect(remaining, dst);
    }

    dst->mixerNode_ = nullptr;
    s->pushDeadNode(mixer);
}

int findFreeMixerSlot(Node* mixer) {
    for (int i = 0; i < (int)mixer->ins.size(); ++i) {
        if (!mixer->ins[i].srcPort_) return i;
    }
    return -1;
}

int countActiveMixerInputs(Node* mixer) {
    int count = 0;
    for (auto& in : mixer->ins) {
        if (in.srcPort_) ++count;
    }
    return count;
}

} // namespace engine
