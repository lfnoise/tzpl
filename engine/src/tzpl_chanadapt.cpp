// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//
//  tzpl_chanadapt.cpp
//  audio engine
//
//  Hidden channel-adapting node (see tzpl_chanadapt.hpp).
//

#include "tzpl_chanadapt.hpp"
#include "tzpl_engine.hpp"

namespace engine {

// ---------------------------------------------------------------------------
// ChanAdaptData -- extends tzpl_SynthData with the two channel counts
// ---------------------------------------------------------------------------

struct ChanAdaptData : tzpl_SynthData {
    int srcChans;
    int dstChans;
    int srcMask;  // srcChans - 1: channel counts are always powers of two
    int dstMask;  // dstChans - 1
};

static tzpl_SynthData* ChanAdapt_alloc() {
    return (tzpl_SynthData*)new ChanAdaptData();
}

static tzpl_SErr ChanAdapt_free(tzpl_SynthData* synth) {
    delete (ChanAdaptData*)synth;
    return tzpl_errNone;
}

static tzpl_SErr ChanAdapt_init(tzpl_SynthData*) { return tzpl_errNone; }
static tzpl_SErr ChanAdapt_uninit(tzpl_SynthData*) { return tzpl_errNone; }

// Widen: every destination channel takes a source channel, wrapping around.
template <typename T>
static void ChanAdapt_widen(tzpl_SynthData* synth) {
    ChanAdaptData* o = (ChanAdaptData*)synth;
    T const* in = getIn<T>(synth, 0);
    T* out = getOut<T>(synth, 0);
    int nd = o->dstChans;
    int mask = o->srcMask;
    for (int c = 0; c < nd; ++c) out[c] = in[c & mask];
}

// Narrow: fold the source onto the destination, summing every channel that
// wraps to the same index.
template <typename T>
static void ChanAdapt_narrow(tzpl_SynthData* synth) {
    ChanAdaptData* o = (ChanAdaptData*)synth;
    T const* in = getIn<T>(synth, 0);
    T* out = getOut<T>(synth, 0);
    int nd = o->dstChans;
    int ns = o->srcChans;
    int mask = o->dstMask;

    for (int c = 0; c < nd; ++c) out[c] = in[c]; // first wrap: plain copy
    for (int j = nd; j < ns; ++j) out[j & mask] += in[j];
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

Node* newChanAdaptNode(Engine* e, Silo* silo,
                       tzpl_SignalType srcType, tzpl_SignalType dstType) {
    bool widening = srcType.chans < dstType.chans;

    tzpl_SynthFuns funs = {0};
    funs.alloc  = ChanAdapt_alloc;
    funs.free   = ChanAdapt_free;
    funs.init   = ChanAdapt_init;
    funs.uninit = ChanAdapt_uninit;

    switch (dstType.elem) {
        case tzpl_kF32:
            funs.processAudio = widening ? ChanAdapt_widen<f32> : ChanAdapt_narrow<f32>;
            break;
        case tzpl_kF64:
            funs.processAudio = widening ? ChanAdapt_widen<f64> : ChanAdapt_narrow<f64>;
            break;
        case tzpl_kI32:
            funs.processAudio = widening ? ChanAdapt_widen<i32> : ChanAdapt_narrow<i32>;
            break;
        case tzpl_kI64:
            funs.processAudio = widening ? ChanAdapt_widen<i64> : ChanAdapt_narrow<i64>;
            break;
    }

    NodeDefInfo info;
    memset(&info, 0, sizeof(info));

    info.name = "chanadapt";
    info.num_ins = 1;
    info.num_outs = 1;
    info.funs = funs;

    info.ins = (PortInfo*)calloc(1, sizeof(PortInfo));
    info.ins[0] = PortInfo{"in", srcType};

    info.outs = (PortInfo*)calloc(1, sizeof(PortInfo));
    info.outs[0] = PortInfo{"out", dstType};

    Node* node = new Node(e, silo, info);
    node->isAdapter_ = true;

    free(info.ins);
    free(info.outs);

    ChanAdaptData* d = (ChanAdaptData*)node->synth;
    d->srcChans = srcType.chans;
    d->dstChans = dstType.chans;
    d->srcMask = srcType.chans - 1;
    d->dstMask = dstType.chans - 1;

    return node;
}

// ---------------------------------------------------------------------------
// Topology helpers
// ---------------------------------------------------------------------------

OutPort* adaptedFrom(OutPort* port) {
    if (!port || !port->node_->isAdapter_) return port;
    OutPort* src = port->node_->ins[0].srcPort_;
    return src ? src : port;
}

void retireAdapter(Silo* s, Node* node) {
    if (!node || !node->isAdapter_ || node->retiring_) return;
    if (node->ins[0].srcPort_ && node->outs[0].dstList_) return; // still in use

    // Claimed first: cutting the other side re-enters through Silo::unlink,
    // which must not recurse or reclaim the node twice.
    node->retiring_ = true;
    s->disconnectNode(node);
    s->pushDeadNode(node);
}

} // namespace engine
