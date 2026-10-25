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
//  Hidden channel-folding node. Broadcasting (a destination at least as
//  large as its source in every dimension) is free pointer remapping in
//  Silo::connect. Where a destination is SMALLER than its source in some
//  dimension, this node sits between them and folds: destination channel w
//  sums every source channel that maps to it (per dimension, source index j
//  lands on j % dst; broadcast dimensions repeat as usual). An 8-channel
//  source into a stereo inlet mixes down to (a0+a2+a4+a6, a1+a3+a5+a7).
//
//  An adapter sits immediately downstream of its source, ahead of any
//  crossfader or fan-in mixer, so everything else in the graph only ever
//  sees shapes that broadcast. It is a hidden node (nodeID -1) and retires
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

// Create a hidden fold adapter from srcType to dstType (same element type).
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
