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
//  A mixer has a fixed number of input slots. Fan-in wider than that is
//  handled by chaining: when every slot is taken, a fresh mixer becomes the
//  new head of the chain and takes the old head's output in one of its own
//  slots. `dst->mixerNode_` always points at the head. Chaining never
//  touches connections that already exist, which is what makes it safe to
//  do on the RT thread while crossfaders are running into mixer slots.
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

// A slot within a mixer chain: which mixer of the chain, and which input.
struct MixerSlot {
    Node* mixer = nullptr;
    int slot = -1;
    explicit operator bool() const { return mixer != nullptr; }
};

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

// RT: Add `newHead` to the front of dst's mixer chain, taking over as the
// destination's source and holding the previous head in its first slot.
// Called when every slot in the existing chain is occupied. On success
// dst->mixerNode_ == newHead and the rest of newHead's slots are free.
tzpl_SErr chainMixer(Silo* s, InPort* dst, Node* newHead);

// RT: Connect an additional source to an existing mixer's free slot.
tzpl_SErr addToMixer(Silo* s, Node* mixer, OutPort* newSrc, int slot);

// RT: Disconnect a specific source from anywhere in the chain.
// Returns false if the source was not found.
bool removeFromMixer(Silo* s, Node* head, OutPort* src);

// RT: Remove the whole chain and restore a direct connection for the single
// remaining source. Sets dst->mixerNode_ = nullptr. Deferred (does nothing)
// while a crossfader is still running into one of the slots.
void collapseMixer(Silo* s, InPort* dst);

// RT: Unconditionally tear down dst's whole mixer chain, along with any
// crossfaders left fading into it (they become unreachable). Used when the
// destination inlet is disconnected outright.
void freeMixerChain(Silo* s, InPort* dst);

// Find an unconnected input slot anywhere in the chain. Falsy if all full.
MixerSlot findFreeMixerSlot(Node* head);

// Find the slot `src` feeds, anywhere in the chain. Falsy if not found.
MixerSlot findMixerSlot(Node* head, OutPort* src);

// Count the real sources feeding the chain (chain links don't count).
int countActiveMixerInputs(Node* head);

// Collect the chain's real sources into `out`, returning how many were
// written (never more than `max`).
int collectMixerSources(Node* head, OutPort** out, int max);

// Walk up from a hidden node (mixer or crossfader) to the destination InPort
// whose mixer chain it belongs to, or null if it feeds something else.
InPort* mixerChainOwner(Node* hidden);

}

#endif /* tzpl_mixer_hpp */
