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
//  tzpl_xfader.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_xfader.hpp"
#include <cstring>
#include "tzpl_engine.hpp"

namespace engine {

struct XFaderBase : tzpl_SynthData
{
    f64 t0 = 0.;        // absolute fractional start time
    i64 count = 1;      // fade length in samples (0 = step)
};

template <std::floating_point T, typename F>
struct XFader : XFaderBase {};

template <std::floating_point T, typename F>
tzpl_SynthData* XFader_alloc(int32_t) {
    return new XFader<T,F>();
}

template <std::floating_point T, typename F>
tzpl_SErr XFader_free(tzpl_SynthData* synth) {
    delete static_cast<XFader<T,F>*>(synth);
    return tzpl_errNone;
}

static tzpl_SErr XFader_init(tzpl_SynthData*) { return tzpl_errNone; }

template <std::floating_point T, typename F>
void XFader_process(tzpl_SynthData* synth, int32_t off, int32_t n) {
    auto* o = static_cast<XFader<T,F>*>(synth);
    Node* node = (Node*)synth->node;
    i64 b0 = node->silo_->blockStart_;

    // One position per sample, shared by every channel.
    T x[1024];
    for (int i = 0; i < n; ++i) x[i] = (T)fadePosition((f64)(b0 + off + i), o->t0, o->count);

    int chans = node->outs[0].chans_;
    for (int c = 0; c < chans; ++c) {
        T const* a = inChan<T>(synth, 0, c) + off;
        T const* b = inChan<T>(synth, 1, c) + off;
        T* out = outChan<T>(synth, 0, c) + off;
        for (int i = 0; i < n; ++i) out[i] = F::fade(x[i], a[i], b[i]);
    }
    if (fadeDoneAfter((f64)(b0 + off + n - 1), o->t0, o->count)) node->faderDone_ = true;
}

template <std::floating_point T, typename F>
Node* newXFaderNodeT(Engine* e, Silo* silo, i64 count, tzpl_SignalType type) {
    tzpl_SynthFuns funs = {0};
    funs.alloc   = XFader_alloc<T,F>;
    funs.free    = XFader_free<T,F>;
    funs.init    = XFader_init;
    funs.process = XFader_process<T,F>;

    // Ports carry the connection's own type: the fade runs over however many
    // channels the destination has.
    PortInfo ins[2] = {PortInfo{"a", type}, PortInfo{"b", type}};
    PortInfo outs[1] = {PortInfo{"out", type}};

    NodeDefInfo info{};
    info.name = "xfader";
    info.num_ins = 2;
    info.num_outs = 1;
    info.ins = ins;
    info.outs = outs;
    info.funs = funs;

    Node* sub = new Node(e, silo, info);
    sub->isXFader_ = true;
    static_cast<XFaderBase*>(sub->synth)->count = count;
    return sub;
}

Node* newXFaderNode(Engine* e, Silo* silo, f64 xfadeTime, FadeCurve curve, tzpl_SignalType type) {
    if (!isFloat(type.elem)) return nullptr;
    f64 fs = e->streamParams_.sampleRate;
    i64 count = xfadeTime > 0. ? std::max((i64)1, (i64)(xfadeTime * fs)) : 0;

    #define NEW_XFADER_T(F) \
        (type.elem == tzpl_kF32 ? newXFaderNodeT<f32, F<f32>>(e, silo, count, type) \
                                : newXFaderNodeT<f64, F<f64>>(e, silo, count, type))
    switch (curve) {
        case fadeLinear:       return NEW_XFADER_T(LinearFade);
        case fadeExponential:  return NEW_XFADER_T(ExponentialFade);
        case fadeSmoothstep:   return NEW_XFADER_T(SmoothStepFade);
        case fadeEqualPower:   return NEW_XFADER_T(EqPowFade);
        case fadeOutIn:        return NEW_XFADER_T(OutInFade);
        case fadeEaseInCubic:  return NEW_XFADER_T(EaseInCubicFade);
        case fadeEaseOutCubic: return NEW_XFADER_T(EaseOutCubicFade);
    }
    #undef NEW_XFADER_T
    return nullptr;
}

// Hold `in` at constants: per channel, values[c] (or dst's stage values).
static void holdConstants(Silo* s, InPort& in, InPort const* dst, f64 const* values) {
    f64 t = s->eventTime();
    for (int c = 0; c < in.chans_; ++c) {
        f64 v = values ? values[c] : dst->valueAt(c, t);
        ChanStage& st = in.stages_[c];
        st.value = v;
        st.ramping = false;
        st.dirty = true;
        st.stamp = -1;
        in.markActive(c);
    }
}

tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* newSrc, InPort* dst, f64 const* endValues) {
    sub->silo_ = s;
    if (!isFloat(dst->type_.elem) || sub->ins[0].chans_ != dst->chans_) {
        s->pushDeadNode(sub);
        return tzpl_errTypeMismatch;
    }
    if (newSrc) {
        tzpl_SErr err = connectableTypes(newSrc->type_, dst->type_);
        if (err == tzpl_errNone && !tzpl::broadcastsTo(newSrc->type_.shape, dst->type_.shape))
            err = tzpl_errChanMismatch;
        if (err != tzpl_errNone) {
            s->pushDeadNode(sub);
            return err;
        }
    }
    static_cast<XFaderBase*>(sub->synth)->t0 = s->eventTime();

    // Connect before disconnecting: if the old source reaches dst through a
    // hidden channel adapter, the adapter must keep a destination at all
    // times or it retires itself out from under this fade.
    OutPort* oldSrc = dst->srcPort_;
    if (oldSrc) {
        s->connect(oldSrc, &sub->ins[0]);
        s->disconnect(dst);
    } else {
        holdConstants(s, sub->ins[0], dst, nullptr);
    }

    if (newSrc) {
        s->connect(newSrc, &sub->ins[1]);
    } else {
        holdConstants(s, sub->ins[1], dst, endValues);
    }
    s->connect(&sub->outs[0], dst);
    return tzpl_errNone;
}

tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* oldSrc, OutPort* newSrc) {
    sub->silo_ = s;
    tzpl_SErr err = compatibleTypes(oldSrc->type_, newSrc->type_);
    if (err == tzpl_errNone && !isFloat(oldSrc->type_.elem)) err = tzpl_errTypeMismatch;
    if (err == tzpl_errNone && sub->ins[0].chans_ != oldSrc->chans_) err = tzpl_errChanMismatch;
    if (err != tzpl_errNone) {
        s->pushDeadNode(sub);
        return err;
    }
    static_cast<XFaderBase*>(sub->synth)->t0 = s->eventTime();

    OutPort* xfadeOut = &sub->outs[0];
    InPort* dst = oldSrc->dstList_;
    while (dst) {
        InPort* next = dst->next_;
        err = s->connect(xfadeOut, dst);
        if (err) return err;
        dst = next;
    }

    err = s->connect(oldSrc, &sub->ins[0]);
    if (err) return err;

    err = s->connect(newSrc, &sub->ins[1]);
    if (err) return err;

    return tzpl_errNone;
}

void retireXFader(Silo* s, Node* sub) {
    sub->faderDone_ = false;
    // Hand the new source to everything the fader fed, then unlink: connect
    // before disconnecting, so a hidden adapter feeding the fade always has
    // a destination. With a constant new value the destinations revert to
    // their own stage values, which the command that started the fade set.
    OutPort* newSrc = sub->ins[1].srcPort_;
    if (newSrc) {
        InPort* dst = sub->outs[0].dstList_;
        while (dst) {
            InPort* next = dst->next_;
            s->connect(newSrc, dst);
            dst = next;
        }
    }
    s->disconnectNode(sub);
    s->sortedListRemove(sub);
    s->pushDeadNode(sub);
}

}
