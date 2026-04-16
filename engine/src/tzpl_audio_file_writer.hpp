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
//  tzpl_audio_file_writer.hpp
//  audio engine
//
//  Minimal float32 WAV writer for non-real-time audio rendering.
//

#ifndef tzpl_audio_file_writer_hpp
#define tzpl_audio_file_writer_hpp

#include "tzpl_common.hpp"

namespace engine {

struct WavWriter;

// Open a 32-bit IEEE float WAV file for writing. Returns nullptr on failure
// (in which case errno is set and an error has been logged to stderr).
// The caller must call wavClose() to finalize the file.
WavWriter* wavOpen(char const* path, int sampleRate, int numChannels);

// Append numFrames frames (interleaved across numChannels) to the file.
// Returns true on success, false on a write error.
bool wavWrite(WavWriter* w, f32 const* interleaved, int numFrames);

// Back-fill the RIFF/data chunk sizes and close the file. Frees the writer.
// Safe to call with nullptr.
void wavClose(WavWriter* w);

}

#endif /* tzpl_audio_file_writer_hpp */
