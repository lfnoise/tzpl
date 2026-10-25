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
//  tzpl_test_plugins.hpp
//  audio engine
//
//  Test/example plugins: SinOsc, AddOp, MulOp, VoicerTest.
//

#ifndef tzpl_test_plugins_h
#define tzpl_test_plugins_h

#include "tzpl_client_interface.hpp"
#include "tzpl_node.hpp"
#include <map>
#include <string>
#include <vector>

namespace engine {

// ---------------------------------------------------------------------------
// Plugin registration functions (ABI v3, hand-written block synths)
// ---------------------------------------------------------------------------

// sinosc: inputs freq, amp (1 channel, specs 261.6 Hz / 0.25); stereo out.
void createSineNode(Engine* e);
// "+" / "*": out = a + b / a * b, stereo f32.
void createAddOpNode(Engine* e);
void createMulOpNode(Engine* e);
// voicerTest: a voiced sine synth. notes {32, 3}: gate, midi note, amp.
// Stereo out.
void createVoicerTestNode(Engine* e);

// A pass-through node (out = in) of the given shape. Left unconnected with
// its inlet set via setInput it is an exact DC source of a chosen shape.
void createPassNode(Engine* e, char const* name, int numChannels);
void createPassNode(Engine* e, char const* name, tzpl_Shape shape, tzpl_ElemType elem = tzpl_kF32);

// counter: 1-channel f32 out; each processed sample outputs how many samples
// this instance processed before it (0, 1, 2, ...). Shows exactly where a
// node started, stopped or was split.
void createCounterNode(Engine* e);

// vtest: a voiced probe. notes {4, 2} (gate, value); out {4} f32. Voice v
// outputs gate * (value + age), where age counts samples since the voice's
// last resetVoice. Records every resetVoice in vtestResets.
void createVoicedTestNode(Engine* e);
struct VTestReset { i64 sample; int voice; };
extern std::vector<VTestReset> vtestResets;

// rec: records every sample of its input (any shape, f32) into
// recordings[name][channel], from its first processed block on; outputs a
// mono 0 so it can be pulled into the graph by connecting it to Audio Out.
void createRecNode(Engine* e, char const* name, tzpl_Shape shape);
extern std::map<std::string, std::vector<std::vector<f32>>> recordings;

} // namespace engine

#endif // tzpl_test_plugins_h
