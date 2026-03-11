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
#include "tzpl_voicer.hpp"

namespace engine {

// Re-import shared voicer types into engine namespace
using ::ShouldCache;
using ::VoicesInRows;
using ::VoicesInColumns;
using ::Voicer;
using ::ColumnVoicer;
using ::RowVoicer;
using ::NextPowerOfTwo;
using ::NextPowerOfTwo_;
using ::nnhz;
using ::calcDecay;
using ::ampdb;

f32 bwarp(f32 x, f32 w);

// ---------------------------------------------------------------------------
// Plugin registration functions
// ---------------------------------------------------------------------------

void createSineNode(Engine* e);
void createAddOpNode(Engine* e);
void createMulOpNode(Engine* e);
void createVoicerTestNode(Engine* e);

} // namespace engine

#endif // tzpl_test_plugins_h
