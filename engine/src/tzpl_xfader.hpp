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
//  tzpl_xfader.hpp
//  audio engine
//
//  Crossfader nodes. A fader is a hidden node spliced between a destination
//  input and its old and new sources: in0 = old (a signal, or constants on
//  the fader's own input stages), in1 = new. It runs a block at a time from
//  a fractional start time (sub-sample fade starts, tzpl_fade_curves.hpp),
//  and retires after the block in which it reaches its end, handing the new
//  source straight to the destination.
//
//  Faders compose: a fade started while another is running into the same
//  input takes the running fader's output as its old source, so three (or
//  N) sources can be audible at once. When the inner fader retires it hands
//  its new source to whatever its output fed -- the outer fader's in0.
//
//  A fade of 0 samples is a step at its start time. Scheduled graph edits
//  use one to take effect mid-block.
//

#ifndef tzpl_xfader_hpp
#define tzpl_xfader_hpp

#include "tzpl_client_interface.hpp"

namespace engine {

struct Engine;
struct Silo;
struct OutPort;
struct InPort;
struct Node;

// NRT: a fader of `type` (float elements only) lasting xfadeTime seconds.
// xfadeTime 0 makes a step fader.
Node* newXFaderNode(Engine* e, Silo* silo, f64 xfadeTime, FadeCurve curve, tzpl_SignalType type);

// RT: fade dst from what feeds it now (its source, or its stage values) to
// newSrc -- or, with newSrc null, to constants: `endValues` (one per channel
// of dst) or, if null, dst's own stage values. Starts at the silo's current
// event time.
tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* newSrc, InPort* dst, f64 const* endValues);
// RT: fade every input oldSrc feeds over to newSrc.
tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* oldSrc, OutPort* newSrc);

// RT, after the block: splice the finished fader out of the graph.
void retireXFader(Silo* s, Node* sub);
}

#endif /* tzpl_xfader_hpp */
