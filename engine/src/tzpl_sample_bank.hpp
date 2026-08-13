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
//  tzpl_sample_bank.hpp
//  Builds a tzpl_SampleBank from a zone spec on the non-real-time side:
//  validates the (pitch, velocity) zones, loads every sound file, and fills
//  the 128x128 lookup map. The finished bank is swapped into a running
//  synth like a tzpl_Buffer (single RT pointer store, old bank freed NRT).
//

#pragma once

#include "tzpl_plugin_abi.h"
#include "tzpl_audio_file.hpp"
#include <span>
#include <string>

namespace engine {

// One zone of a sample bank spec: a sound file covering an inclusive
// pitch/velocity rectangle. rootKey -1 means "default to loKey".
struct SampleBankZoneSpec {
    std::string path;
    int loKey = 0;
    int hiKey = 127;
    int loVel = 0;
    int hiVel = 127;
    int rootKey = -1;
};

// Validates the zones and builds a complete bank: every file loaded, the
// map painted, unmapped cells left at TZPL_SAMPLEBANK_UNMAPPED (reads
// through those cells yield silence). Zones must lie within 0..127 with
// lo <= hi, and each zone's (pitch, velocity) rectangle is a tile: no two
// tiles may cover the same cell. Pitch ranges MAY overlap between zones
// whose velocity ranges are disjoint (velocity layering), and vice versa.
// On any failure everything allocated so far is freed, nullptr is
// returned, and errOut (if non-null) receives tzpl_errBadSampleBank.
inline tzpl_SampleBank* buildSampleBank(std::span<SampleBankZoneSpec const> zones,
                                        tzpl_SErr* errOut) {
    auto fail = [&](tzpl_SampleBank* bank) -> tzpl_SampleBank* {
        tzpl_freeSampleBank(bank);
        if (errOut) *errOut = tzpl_errBadSampleBank;
        return nullptr;
    };

    if (zones.empty() || zones.size() >= TZPL_SAMPLEBANK_UNMAPPED) return fail(nullptr);

    tzpl_SampleBank* bank = tzpl_createSampleBank((int64_t)zones.size());
    if (!bank) return fail(nullptr);

    // Validate every zone and paint the map before any file I/O, so cheap
    // spec errors (bad ranges, tile overlap) never touch the filesystem.
    for (size_t zi = 0; zi < zones.size(); ++zi) {
        SampleBankZoneSpec const& z = zones[zi];
        if (z.loKey < 0 || z.hiKey > 127 || z.loKey > z.hiKey) return fail(bank);
        if (z.loVel < 0 || z.hiVel > 127 || z.loVel > z.hiVel) return fail(bank);
        if (z.rootKey < -1 || z.rootKey > 127) return fail(bank);

        // Paint the zone's cells; a cell already painted means overlap.
        for (int p = z.loKey; p <= z.hiKey; ++p) {
            for (int v = z.loVel; v <= z.hiVel; ++v) {
                uint16_t& cell = bank->map[p * 128 + v];
                if (cell != TZPL_SAMPLEBANK_UNMAPPED) return fail(bank);
                cell = (uint16_t)zi;
            }
        }
    }

    for (size_t zi = 0; zi < zones.size(); ++zi) {
        SampleBankZoneSpec const& z = zones[zi];
        double sr = 0.0;
        tzpl_Buffer* buf = tzpl_loadAudioFileSR(z.path.c_str(), 0, 0, INT64_MAX, &sr);
        if (!buf) return fail(bank);
        bank->samples[zi].buf = buf;
        bank->samples[zi].sampleRate = sr;
        bank->samples[zi].rootKey = (float)(z.rootKey >= 0 ? z.rootKey : z.loKey);
    }

    if (errOut) *errOut = tzpl_errNone;
    return bank;
}

} // namespace engine
