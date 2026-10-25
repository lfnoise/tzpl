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
#include <cstring>
#include "tzpl_engine.hpp"

namespace engine {

// ---------------------------------------------------------------------------
// ChanAdaptData -- the fold table, CSR: destination channel w sums source
// channels src[start[w] .. start[w+1]).
// ---------------------------------------------------------------------------

struct ChanAdaptData : tzpl_SynthData {
    std::vector<int> start;
    std::vector<int> src;
};

static tzpl_SynthData* ChanAdapt_alloc(int32_t) {
    return new ChanAdaptData();
}

static tzpl_SErr ChanAdapt_free(tzpl_SynthData* synth) {
    delete static_cast<ChanAdaptData*>(synth);
    return tzpl_errNone;
}

static tzpl_SErr ChanAdapt_init(tzpl_SynthData*) { return tzpl_errNone; }

template <typename T>
static void ChanAdapt_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    auto* o = static_cast<ChanAdaptData*>(synth);
    int nd = (int)o->start.size() - 1;
    for (int w = 0; w < nd; ++w) {
        T* out = outChan<T>(synth, 0, w) + off;
        int s0 = o->start[w], s1 = o->start[w + 1];
        if (s0 == s1) {
            for (int k = 0; k < n; ++k) out[k] = T(0);
            continue;
        }
        T const* in = inChan<T>(synth, 0, o->src[s0]) + off;
        for (int k = 0; k < n; ++k) out[k] = in[k];
        for (int j = s0 + 1; j < s1; ++j) {
            in = inChan<T>(synth, 0, o->src[j]) + off;
            for (int k = 0; k < n; ++k) out[k] += in[k];
        }
    }
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

Node* newChanAdaptNode(Engine* e, Silo* silo,
                       tzpl_SignalType srcType, tzpl_SignalType dstType) {
    tzpl_SynthFuns funs = {0};
    funs.alloc  = ChanAdapt_alloc;
    funs.free   = ChanAdapt_free;
    funs.init   = ChanAdapt_init;

    switch (dstType.elem) {
        case tzpl_kF32: funs.process = ChanAdapt_process<f32>; break;
        case tzpl_kF64: funs.process = ChanAdapt_process<f64>; break;
        case tzpl_kI32: funs.process = ChanAdapt_process<i32>; break;
        case tzpl_kI64: funs.process = ChanAdapt_process<i64>; break;
    }

    PortInfo in{"in", srcType};
    PortInfo out{"out", dstType};
    NodeDefInfo info{};
    info.name = "chanadapt";
    info.num_ins = 1;
    info.num_outs = 1;
    info.funs = funs;
    info.ins = &in;
    info.outs = &out;

    Node* node = new Node(e, silo, info);
    node->isAdapter_ = true;

    // Destination channel w sums source channel j when, in every dimension,
    // folding dims satisfy j_d % D_d == w_d and broadcast dims j_d == w_d % S_d.
    tzpl_Shape S = srcType.shape, D = dstType.shape;
    int ns = tzpl::chans(S), nd = tzpl::chans(D);
    auto* d = static_cast<ChanAdaptData*>(node->synth);
    d->start.assign(nd + 1, 0);
    for (int w = 0; w < nd; ++w) {
        int wi[tzpl::kMaxRank];
        tzpl::index(D, w, wi);
        d->start[w] = (int)d->src.size();
        for (int j = 0; j < ns; ++j) {
            int ji[tzpl::kMaxRank];
            tzpl::index(S, j, ji);
            bool hit = true;
            for (int k = 0; k < tzpl::kMaxRank && hit; ++k) {
                int sd = (int)S.dims[k], dd = (int)D.dims[k];
                hit = dd < sd ? ji[k] % dd == wi[k] : ji[k] == wi[k] % sd;
            }
            if (hit) d->src.push_back(j);
        }
    }
    d->start[nd] = (int)d->src.size();
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
