//
//  jscs_xfader.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "jscs_xfader.hpp"
#include "jscs_engine.hpp"

namespace engine {

template <std::floating_point T>
T smoothStep(T x) {
    return x * x * (T(3) - T(2) * x);
}

template <std::floating_point T>
T panfun(T x) {
    static constexpr T a = 0.337403011047526069;
    static constexpr T b = -1.334338069017510398;
    static constexpr T c = -a-b-T(1); // a+b+c = -1.
    return T(1) + x * (c + x * (b + x * a));
}

template <std::floating_point T>
T cubed(T x) { return x*x*x; }

template <std::floating_point T>
struct LinearFade {
    static T fade(T x, T a, T b) {
        return a + x * (b - a);
    }
};

template <std::floating_point T>
struct ExponentialFade {
    static T fade(T x, T a, T b) {
        return a * pow(b/a, x);
    }
};

template <std::floating_point T>
struct SmoothStepFade {
    static T fade(T x, T a, T b) {
        return a + smoothStep(x) * (b - a);
    }
};

template <std::floating_point T>
struct OutInFade {
    static T fade(T x, T a, T b) {
        return x < 0.5 ? smoothStep(T(1) - T(2)*x) * a : smoothStep(T(2)*x - T(1)) * b;
    }
};

template <std::floating_point T>
struct EqPowFade {
    static T fade(T x, T a, T b) {
        return a * panfun(T(1) - x) + b * panfun(x);
    }
};

template <std::floating_point T>
struct EaseInCubicFade {
    static T fade(T x, T a, T b) {
        return a + cubed(x) * (b - a);
    }
};

template <std::floating_point T>
struct EaseOutCubicFade {
    static T fade(T x, T a, T b) {
        return b + cubed(T(1) - x) * (a - b);
    }
};

template <std::floating_point T, typename F>
struct XFader : jscs_SynthData
{
    i64 count;
    f64 slope;
    f64 xfade = 0.;
    int numChannels;
    T* norm;

    void fade(T const* ax, T const* bx) {
        T* out = getOut<T>(this, 0);
        for (int i = 0; i < numChannels; ++i) {
            T a = ax[i];
            T b = bx[i];
            out[i] = F::fade(xfade, a, b);
        }
    }
};

template <std::floating_point T, typename F>
jscs_SynthData* XFader_alloc() {
    return (jscs_SynthData*)new XFader<T,F>();
}

template <std::floating_point T, typename F>
jscs_SErr XFader_free(jscs_SynthData* synth) {
    delete (XFader<T,F>*)synth;
    return jscs_errNone;
}

template <std::floating_point T, typename F>
jscs_SErr XFader_init(jscs_SynthData* synth) {
    return jscs_errNone;
}

template <std::floating_point T, typename F>
jscs_SErr XFader_uninit(jscs_SynthData* synth) {
    return jscs_errNone;
}

template <std::floating_point T, typename F>
void XFadeTwo_processAudio(jscs_SynthData* synth) {
    XFader<T,F>* o = (XFader<T,F>*)synth;
    T const* a = (T const*)getInput(synth, 0);
    T const* b = (T const*)getInput(synth, 1);

    o->fade(a, b);

    Node* sub = (Node*)synth->node;
    if (--o->count <= 0) {
        Silo* s = sub->silo_;
        InPort* dstPort = sub->outs[0].dstList_;
        OutPort* srcPort = sub->ins[1].srcPort_;
        s->disconnectNode(sub); // unlink self.
        s->connect(srcPort, dstPort); // splice new source around self directly to destination.
        s->pushDeadNode(sub);
    } else {
        o->xfade += o->slope;
    }
}

template <std::floating_point T, typename F>
void XFadeIn_processAudio(jscs_SynthData* synth) {
    XFader<T,F>* o = (XFader<T,F>*)synth;
    T const* a = o->norm;
    T const* b = (T const*)getInput(synth, 1);

    o->fade(a, b);

    Node* sub = (Node*)synth->node;
    if (--o->count <= 0) {
        Silo* s = sub->silo_;
        InPort* dstPort = sub->outs[0].dstList_;
        OutPort* srcPort = sub->ins[1].srcPort_;
        s->disconnectNode(sub); // unlink self.
        s->connect(srcPort, dstPort); // splice new source around self directly to destination.
        s->pushDeadNode(sub);
    } else {
        o->xfade += o->slope;
    }
}

template <std::floating_point T, typename F>
void XFadeOut_processAudio(jscs_SynthData* synth) {
    XFader<T,F>* o = (XFader<T,F>*)synth;
    T const* a = (T const*)getInput(synth, 0);
    T const* b = getIn<T>(o, 1);

    o->fade(a, b);

    Node* sub = (Node*)synth->node;
    if (--o->count <= 0) {
        Silo* s = sub->silo_;
        s->disconnectNode(sub); // unlink self.
        s->pushDeadNode(sub);
    } else {
        o->xfade += o->slope;
    }
}

template <std::floating_point T, typename F>
void XFadeSet_processAudio(jscs_SynthData* synth) {
    XFader<T,F>* o = (XFader<T,F>*)synth;
    T const* a = getIn<T>(o, 0);
    T const* b = getIn<T>(o, 1);
    
    o->fade(a, b);

    Node* sub = (Node*)synth->node;
    if (--o->count <= 0) {
        Silo* s = sub->silo_;
        s->disconnectNode(sub); // unlink self.
        s->pushDeadNode(sub);
    } else {
        o->xfade += o->slope;
    }
}

template <std::floating_point T, typename F>
Node* newXFaderNodeT(Engine* e, Silo* silo, f64 xfadeTime, jscs_SignalType type) {
    f64 fs = e->streamParams_.sampleRate;
    i64 count = std::max((i64)1, (i64)(xfadeTime * fs));
    
    jscs_SynthFuns funs = {0};
    funs.alloc  = XFader_alloc<T,F>;
    funs.free   = XFader_free<T,F>;
    funs.init   = XFader_init<T,F>;
    funs.uninit = XFader_uninit<T,F>;
        
    NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    
    info.name = "xfader";
    info.num_ins = 2;
    info.num_outs = 1;
    
    PortInfo a{"a", {type.elem, jscs_audioRate, 2}};
    PortInfo b{"b", {type.elem, jscs_audioRate, 2}};
    info.ins = (PortInfo*)calloc(info.num_ins, sizeof(PortInfo));
    info.ins[0] = a;
    info.ins[1] = b;
    
    PortInfo out{"out", {type.elem, jscs_audioRate, 2}};
    info.outs = (PortInfo*)calloc(info.num_outs, sizeof(PortInfo));
    info.outs[0] = out;
    info.funs = funs;
    
    Node* sub = new Node(e, silo, info);
    
    free(info.ins);
    free(info.outs);

    InPort srcA, srcB;
    srcA.node_ = srcB.node_ = sub;
    srcA.type_ = srcB.type_ = type;
    srcA.index_ = 0;
    srcB.index_ = 1;

    OutPort outPort;
    outPort.node_  = sub;
    outPort.index_ = 0;

    sub->ins.push_back(srcA);
    sub->ins.push_back(srcB);
    sub->outs.push_back(outPort);    

    XFader<T,F>* xfader = (XFader<T,F>*)sub->synth;

    xfader->numChannels = type.chans;
    
    xfader->count = count;
    xfader->slope = 1. / (f64)(count + 1);

    
    return sub;
}

Node* newXFaderNode(Engine* e, Silo* silo, f64 xfadeTime, FadeCurve curve, jscs_SignalType type) {
    #define CALL_NEW_XFADER_T(TYPE, F) \
        (TYPE.elem == jscs_kF32 ? newXFaderNodeT<f32, F<f32>>(e, silo, xfadeTime, TYPE) \
                                : newXFaderNodeT<f64, F<f64>>(e, silo, xfadeTime, TYPE))
    switch (curve) {
        case fadeLinear :
            return CALL_NEW_XFADER_T(type, LinearFade);
        case fadeExponential :
            return CALL_NEW_XFADER_T(type, ExponentialFade);
        case fadeSmoothstep :
            return CALL_NEW_XFADER_T(type, SmoothStepFade);
        case fadeEqualPower :
            return CALL_NEW_XFADER_T(type, EqPowFade);
        case fadeOutIn :
            return CALL_NEW_XFADER_T(type, OutInFade);
        case fadeEaseInCubic :
            return CALL_NEW_XFADER_T(type, EaseInCubicFade);
        case fadeEaseOutCubic :
            return CALL_NEW_XFADER_T(type, EaseOutCubicFade);
    }
}

template <std::floating_point T, typename F>
jscs_SErr setupXFaderT(Silo* s, Node* sub, OutPort* newSrc, InPort* dst, void* values) {
    
    XFader<T,F>* xfader = (XFader<T,F>*)sub->synth;

    xfader->norm = (T*)dst->dataBuffer_;
        
    sub->ins[0].type_ = sub->ins[1].type_ = sub->outs[0].type_ = dst->type_;

    OutPort* oldSrc = dst->srcPort_;
    s->disconnect(dst);
    
    int byteSize = calcByteSize(dst->type_);
    if (newSrc) {
        if (oldSrc) {
            sub->funs.processAudio = XFadeTwo_processAudio<T,F>;
            memcpy(sub->ins[0].dataBuffer_, dst->dataBuffer_, byteSize);
        } else {
            sub->funs.processAudio = XFadeIn_processAudio<T,F>;
        }
    } else {
        if (oldSrc) {
            sub->funs.processAudio = XFadeOut_processAudio<T,F>;
            memcpy(sub->ins[0].dataBuffer_, dst->dataBuffer_, byteSize);
            memcpy(getIn<T>(xfader, 1), values, byteSize);
        } else {
            sub->funs.processAudio = XFadeSet_processAudio<T,F>;
            memcpy(getIn<T>(xfader, 0), dst->dataBuffer_, byteSize);
            memcpy(getIn<T>(xfader, 1), values, byteSize);
        }
    }
    
    if (oldSrc) {
        s->connect(oldSrc, &sub->ins[0]);
    }
    if (newSrc) {
        s->connect(newSrc, &sub->ins[1]);
    }
    s->connect(&sub->outs[0], dst);
    
    return jscs_errNone;
}


jscs_SErr setupXFader(Silo* s, Node* sub, OutPort* newSrc, InPort* dst, FadeCurve curve, void* values) {
    sub->silo_ = s;
    
    // call in RT thread.
    if (!isFloat(dst->type_.elem)) {
        s->pushDeadNode(sub);
        return jscs_errTypeMismatch;
    }
    if (newSrc) {
        jscs_SErr err = compatibleTypes(newSrc->type_, dst->type_);
        if (err != jscs_errNone) {
            s->pushDeadNode(sub);
            return jscs_errTypeMismatch;
        }
    }
 
#define CALL_SETUP_XFADER_T(E, F) \
    (E == jscs_kF32 ? setupXFaderT<f32, F<f32>>(s, sub, newSrc, dst, values) \
                            : setupXFaderT<f64, F<f64>>(s, sub, newSrc, dst, values))
    
    auto elem = dst->type_.elem;
    
    switch (curve) {
        case fadeLinear :
            return CALL_SETUP_XFADER_T(elem, LinearFade);
        case fadeExponential :
            return CALL_SETUP_XFADER_T(elem, ExponentialFade);
        case fadeSmoothstep :
            return CALL_SETUP_XFADER_T(elem, SmoothStepFade);
        case fadeEqualPower :
            return CALL_SETUP_XFADER_T(elem, EqPowFade);
        case fadeOutIn :
            return CALL_SETUP_XFADER_T(elem, OutInFade);
        case fadeEaseInCubic :
            return CALL_SETUP_XFADER_T(elem, EaseInCubicFade);
        case fadeEaseOutCubic :
            return CALL_SETUP_XFADER_T(elem, EaseOutCubicFade);
    }
}


template <std::floating_point T, typename F>
jscs_SErr setupXFaderT(Silo* s, Node* sub, OutPort* oldSrc, OutPort* newSrc) {
    
    //XFader<T,F>* xfader = (XFader<T,F>*)sub->data;
        
    sub->ins[0].type_ = sub->ins[1].type_ = sub->outs[0].type_ = newSrc->type_;
    
    sub->funs.processAudio = XFadeTwo_processAudio<T,F>;
    
    jscs_SErr err;

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

    return jscs_errNone;
}

jscs_SErr setupXFader(Silo* s, Node* sub, OutPort* oldSrc, OutPort* newSrc, FadeCurve curve) {
    sub->silo_ = s;
    
    // call in RT thread.
    if (!isFloat(oldSrc->type_.elem) || !isFloat(newSrc->type_.elem)) {
        s->pushDeadNode(sub);
        return jscs_errTypeMismatch;
    }
    if (newSrc) {
        jscs_SErr err = compatibleTypes(oldSrc->type_, newSrc->type_);
        if (err != jscs_errNone) {
            s->pushDeadNode(sub);
            return jscs_errTypeMismatch;
        }
    }
 
#define CALL_SETUP_XFADER_T2(E, F) \
    (E == jscs_kF32 ? setupXFaderT<f32, F<f32>>(s, sub, oldSrc, newSrc) \
                            : setupXFaderT<f64, F<f64>>(s, sub, oldSrc, newSrc))
    
    auto elem = oldSrc->type_.elem;
    
    switch (curve) {
        case fadeLinear :
            return CALL_SETUP_XFADER_T2(elem, LinearFade);
        case fadeExponential :
            return CALL_SETUP_XFADER_T2(elem, ExponentialFade);
        case fadeSmoothstep :
            return CALL_SETUP_XFADER_T2(elem, SmoothStepFade);
        case fadeEqualPower :
            return CALL_SETUP_XFADER_T2(elem, EqPowFade);
        case fadeOutIn :
            return CALL_SETUP_XFADER_T2(elem, OutInFade);
        case fadeEaseInCubic :
            return CALL_SETUP_XFADER_T2(elem, EaseInCubicFade);
        case fadeEaseOutCubic :
            return CALL_SETUP_XFADER_T2(elem, EaseOutCubicFade);
    }
    return jscs_errNone;
}

}

