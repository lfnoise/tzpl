// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//
//  tzpl_mixer.hpp
//  audio engine
//
//  Hidden mixer (sum) node for transparent fan-in. When multiple sources
//  connect to the same InPort, a mixer node is automatically interposed.
//  The mixer sums all its inputs and drives the destination. When only
//  one source remains the mixer is collapsed back to a direct connection.
//

#ifndef tzpl_mixer_hpp
#define tzpl_mixer_hpp

#include "tzpl_client_interface.hpp"

namespace engine {

struct Engine;
struct Silo;
struct OutPort;
struct InPort;
struct Node;

// Create a hidden mixer node with the given capacity (number of input slots).
// Allocated in NRT. The node has nodeID == -1 (hidden, like xfader sub-nodes).
Node* newMixerNode(Engine* e, Silo* silo, tzpl_SignalType type, int capacity);

// RT: Splice a new mixer between an existing source and a destination.
// Called when a second source connects to an InPort that already has one.
//   - Moves the existing source to mixer.in[0]
//   - Connects newSrc to mixer.in[1]
//   - Connects mixer.out to dst
//   - Sets dst->mixerNode_ = mixer
tzpl_SErr spliceMixer(Silo* s, Node* mixer, OutPort* newSrc, InPort* dst);

// RT: Connect an additional source to an existing mixer's free slot.
tzpl_SErr addToMixer(Silo* s, Node* mixer, OutPort* newSrc, int slot);

// RT: Disconnect a specific source from a mixer input.
// Returns the slot index that was freed, or -1 if not found.
int removeFromMixer(Silo* s, Node* mixer, OutPort* src);

// RT: Remove the mixer and restore a direct connection for the single
// remaining source. Sets dst->mixerNode_ = nullptr.
void collapseMixer(Silo* s, InPort* dst);

// Find an unconnected input slot on the mixer. Returns -1 if full.
int findFreeMixerSlot(Node* mixer);

// Count how many mixer inputs are currently connected.
int countActiveMixerInputs(Node* mixer);

}

#endif /* tzpl_mixer_hpp */
