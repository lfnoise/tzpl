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
//  juce_audio_backend.hpp
//  app (JUCE)
//
//  Factory for the JUCE AudioDeviceManager engine backend. The class itself
//  lives in the .cpp so that main.cpp can install the backend without
//  including any JUCE headers.
//

#ifndef juce_audio_backend_hpp
#define juce_audio_backend_hpp

#include "tzpl_audio_backend.hpp"
#include <memory>

namespace tzplapp {

std::unique_ptr<engine::AudioBackend> makeJuceAudioBackend();

}

#endif /* juce_audio_backend_hpp */
