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
//  Created by James McCartney on 7/16/25.
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

Node* newXFaderNode(Engine* e, Silo* silo, f64 xfadeTime, FadeCurve curve, tzpl_SignalType type);
tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* newSrc, InPort* dst, FadeCurve curve, void* values);
tzpl_SErr setupXFader(Silo* s, Node* sub, OutPort* oldSrc, OutPort* newSrc, FadeCurve curve);
}

#endif /* tzpl_xfader_hpp */
