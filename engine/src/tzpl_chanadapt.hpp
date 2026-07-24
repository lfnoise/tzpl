// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//
//  tzpl_chanadapt.hpp
//  audio engine
//
//  Hidden channel-adapting node. Interposed automatically when a source's
//  channel count differs from its destination's, so any outlet can drive any
//  inlet of the same element type and rate.
//
//    widening (src < dst):  out[c] = in[c & (srcChans-1)]
//        a mono source feeds every destination channel; a stereo source into
//        a quad inlet lands as (L, R, L, R).
//
//    narrowing (src > dst): out[c] = sum of in[j] for every j & (dstChans-1) == c
//        an 8-channel source into a stereo inlet mixes down to
//        (a0+a2+a4+a6, a1+a3+a5+a7).
//
//  The two rules are duals -- both wrap channel index c around the smaller
//  count -- and agree with a plain copy when the counts are equal (in which
//  case no adapter is inserted at all). Channel counts are required to be
//  powers of two, so the wrap is a mask.
//
//  An adapter sits immediately downstream of its source, ahead of any
//  crossfader or fan-in mixer, so everything else in the graph only ever
//  sees matching channel counts. It is a hidden node (nodeID -1) and retires
//  itself as soon as either side of it is disconnected (see Silo::unlink).
//

#ifndef tzpl_chanadapt_hpp
#define tzpl_chanadapt_hpp

#include "tzpl_client_interface.hpp"

namespace engine {

struct Engine;
struct Silo;
struct Node;
struct OutPort;

// Create a hidden adapter converting srcType to dstType. The two types must
// agree on element type and rate; only the channel counts may differ.
// Allocated in NRT, like mixers and crossfaders.
Node* newChanAdaptNode(Engine* e, Silo* silo,
                       tzpl_SignalType srcType, tzpl_SignalType dstType);

// The outlet a port ultimately comes from: for an adapter's outlet, the
// source feeding the adapter; for anything else, the port itself. Lets the
// rest of the engine keep speaking in terms of the user's own nodes.
OutPort* adaptedFrom(OutPort* port);

// RT: unlink and reclaim `node` if it is an adapter that has lost either its
// source or its destination. Called from Silo::unlink, so adapters never
// outlive the connection they were created for.
void retireAdapter(Silo* s, Node* node);

}

#endif /* tzpl_chanadapt_hpp */
