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
//  tzpl_note_voicer.hpp
//  audio engine
//
//  Engine-side voice allocation for voiced synths (block engine, D2). The
//  synth has a `notes` input of shape {maxVoices, 1+N}; column 0 is the gate.
//  The voicer only decides which row a note gets. The engine then splits the
//  node at the onset (resetVoice) and writes the row as input-stage steps.
//
//  The allocation policy is shared/tzpl_voicer.hpp's, with run-time sizes:
//  take the voice released longest ago, else steal the one started longest
//  ago, scanning from a rotating start so voices freed at the same time are
//  handed out in turn.
//

#ifndef tzpl_note_voicer_hpp
#define tzpl_note_voicer_hpp

#include "tzpl_common.hpp"
#include <limits>
#include <vector>

namespace engine {

struct NoteVoicer {
    int maxVoices;
    int numParams;
    std::vector<int> noteID;
    std::vector<i64> onTime;
    std::vector<i64> offTime;
    std::vector<u8> gate;
    int scanStart = 0;
    int active = 0;

    NoteVoicer(int voices, int params)
        : maxVoices(voices < 1 ? 1 : voices), numParams(params < 0 ? 0 : params),
          noteID(maxVoices, 0), onTime(maxVoices, 0), offTime(maxVoices, 0),
          gate(maxVoices, 0)
    {}

    int find(int id) const {
        if (id < 0) return -1;
        for (int v = 0; v < maxVoices; ++v)
            if (noteID[v] == id) return v;
        return -1;
    }

    // A voice for a new note. Ends any voice already holding `id` (marked
    // replaced with id -1); `ended` receives that voice if its gate was on,
    // so the caller can write its gate off.
    int noteOn(i64 now, int id, int& ended) {
        ended = -1;
        for (int v = 0; v < maxVoices; ++v) {
            if (noteID[v] == id) {
                if (gate[v]) {
                    gate[v] = 0;
                    --active;
                    offTime[v] = now;
                    ended = v;
                }
                noteID[v] = -1;
            }
        }
        i64 minOff = std::numeric_limits<i64>::max();
        i64 minOn = std::numeric_limits<i64>::max();
        int offIdx = -1, onIdx = -1;
        for (int k = 0; k < maxVoices; ++k) {
            int v = (scanStart + k) % maxVoices;
            if (gate[v]) {
                if (onTime[v] < minOn) { minOn = onTime[v]; onIdx = v; }
            } else {
                if (offTime[v] < minOff) { minOff = offTime[v]; offIdx = v; }
            }
        }
        int v = offIdx >= 0 ? offIdx : onIdx;
        scanStart = (v + 1) % maxVoices;
        if (!gate[v]) ++active;
        gate[v] = 1;
        onTime[v] = now;
        noteID[v] = id;
        return v;
    }

    // The voice released, or -1 if `id` is not sounding.
    int noteOff(i64 now, int id) {
        int v = find(id);
        if (v < 0 || !gate[v]) return -1;
        gate[v] = 0;
        --active;
        offTime[v] = now;
        return v;
    }
};

} // namespace engine

#endif // tzpl_note_voicer_hpp
