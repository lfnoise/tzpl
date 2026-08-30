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
//  test_voicer.cpp
//  integration-tests
//
//  Voicer allocator invariants (no audio; pure bookkeeping over the shared
//  header). noteID ownership transfers at noteOn: re-striking an ID releases
//  the sustaining voice that held it (instead of orphaning it until
//  allNotesOff) and clears the ID from every replaced voice, so by-ID lookups
//  (noteOff, setNoteParams) can only ever reach the newest note. A released
//  note keeps its ID -- and stays addressable -- through its decay, until it
//  is replaced. noteOff on an already-released match is not-found rather than
//  a second activeVoices_ decrement.
//
//  Regression: duplicate noteOns used to allocate a second voice with the
//  same ID; noteOff could only reach one of them and the other sustained
//  until allNotesOff (2026-08-25).
//

#include "tzpl_voicer.hpp"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

int main() {
    constexpr int kVoices = 8, kParams = 2;
    RowVoicer<kVoices, kParams> v;
    f32 storage[kVoices][1 + kParams] = {};
    v.setParams(&storage[0][0]);
    f32 p[kParams] = {60.f, 0.5f};

    auto totalGatesOn = [&] {
        int n = 0;
        for (int i = 0; i < kVoices; ++i) if (v.get(i, 0) > 0.f) ++n;
        return n;
    };

    int vi = -1;

    // Duplicate noteOn: the old voice must be released, not orphaned.
    CHECK(v.noteOn(100, 42, kParams, p, vi) == tzpl_errNone);
    CHECK(totalGatesOn() == 1);
    CHECK(v.activeVoices() == 1);
    CHECK(v.noteOn(200, 42, kParams, p, vi) == tzpl_errNone);
    CHECK(totalGatesOn() == 1);          // old 42 released, new 42 sustaining
    CHECK(v.activeVoices() == 1);

    // One noteOff releases the remaining voice with that ID.
    CHECK(v.noteOff(300, 42) == tzpl_errNone);
    CHECK(totalGatesOn() == 0);
    CHECK(v.activeVoices() == 0);

    // A second noteOff must not match the released voice again.
    CHECK(v.noteOff(400, 42) == tzpl_errNoteNotFound);
    CHECK(v.activeVoices() == 0);        // no double decrement

    // Distinct IDs are unaffected.
    CHECK(v.noteOn(500, 1, kParams, p, vi) == tzpl_errNone);
    CHECK(v.noteOn(500, 2, kParams, p, vi) == tzpl_errNone);
    CHECK(totalGatesOn() == 2);
    CHECK(v.activeVoices() == 2);
    CHECK(v.noteOff(600, 1) == tzpl_errNone);
    CHECK(v.noteOff(600, 2) == tzpl_errNone);
    CHECK(totalGatesOn() == 0);
    CHECK(v.activeVoices() == 0);

    // Triple strike of the same ID still leaves exactly one sustaining voice.
    CHECK(v.noteOn(700, 7, kParams, p, vi) == tzpl_errNone);
    CHECK(v.noteOn(710, 7, kParams, p, vi) == tzpl_errNone);
    CHECK(v.noteOn(720, 7, kParams, p, vi) == tzpl_errNone);
    CHECK(totalGatesOn() == 1);
    CHECK(v.activeVoices() == 1);
    CHECK(v.noteOff(730, 7) == tzpl_errNone);
    CHECK(totalGatesOn() == 0);

    // A released note keeps its ID through its decay: it remains addressable
    // for param changes until replaced.
    int vi9 = -1;
    CHECK(v.noteOn(800, 9, kParams, p, vi9) == tzpl_errNone);
    CHECK(v.noteOff(810, 9) == tzpl_errNone);
    CHECK(v.findVoice(9) == vi9);        // still addressable while decaying

    // Replacement transfers ID ownership: after a new noteOn(9), by-ID lookup
    // reaches ONLY the new voice, even when the cache entry is evicted and
    // findVoice falls back to the linear scan. kCacheSize here is
    // nextpow2(4*8) = 32, so ID 9+32 collides with ID 9 in the cache.
    int vi9b = -1, viCollide = -1;
    CHECK(v.noteOn(820, 9, kParams, p, vi9b) == tzpl_errNone);
    CHECK(vi9b != vi9);                  // decaying voice not stolen, new one allocated
    CHECK(v.noteOn(830, 9 + 32, kParams, p, viCollide) == tzpl_errNone); // evict cache slot
    CHECK(v.findVoice(9) == vi9b);       // linear scan finds the replacement, not the tail
    CHECK(v.findVoice(9 + 32) == viCollide);

    // Negative IDs never match, even against replaced voices.
    CHECK(v.findVoice(-1) == -1);

    if (failures == 0) { std::printf("VOICER TEST PASS\n"); return 0; }
    std::printf("VOICER TEST FAIL (%d)\n", failures);
    return 1;
}
