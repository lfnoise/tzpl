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
//  tzpl_audio_file_meta.hpp
//  Instrument metadata (root key, sustain loop) from audio file chunks.
//
//  A small self-contained chunk walker -- deliberately not the platform
//  decoder: ExtAudioFile does not surface the WAV "smpl" / AIFF "INST"
//  chunks at all, and going through libsndfile just for these two fixed
//  structs would add a dependency the macOS build otherwise doesn't have.
//  Reading them directly also makes both platforms behave identically.
//
//  WAV:  the "smpl" chunk -- MIDI unity note + pitch fraction, and loops
//        with a per-loop dwFraction sub-sample end offset.
//  AIFF: the "INST" chunk (baseNote, detune, sustain loop marker ids)
//        resolved through the "MARK" chunk (marker id -> frame).
//  Other containers (mp3, m4a, caf, flac, ...): no metadata.
//

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

// Absent fields are negative: rootKey -1 when the file declares no root,
// loopStart -1 when it declares no sustain loop. loopEnd is EXCLUSIVE and
// may be fractional (smpl dwFraction); rootKey may be fractional too
// (smpl dwMIDIPitchFraction, AIFF detune).
struct tzpl_AudioFileMeta {
    float rootKey = -1.0f;
    double loopStart = -1.0;
    double loopEnd = -1.0;
};

namespace tzpl_meta_detail {

inline uint16_t rd_u16le(unsigned char const* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
inline uint32_t rd_u32le(unsigned char const* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint16_t rd_u16be(unsigned char const* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
inline uint32_t rd_u32be(unsigned char const* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

inline bool readAt(FILE* f, long offset, void* out, size_t n) {
    return fseek(f, offset, SEEK_SET) == 0 && fread(out, 1, n, f) == n;
}

// --- WAV -----------------------------------------------------------------
//
// smpl chunk layout (all little-endian u32 unless noted):
//   0 dwManufacturer  4 dwProduct       8 dwSamplePeriod
//  12 dwMIDIUnityNote 16 dwMIDIPitchFraction (fraction of a semitone / 2^32)
//  20 dwSMPTEFormat   24 dwSMPTEOffset
//  28 cSampleLoops    32 cbSamplerData
//  then per loop (24 bytes):
//   0 dwIdentifier  4 dwType (0 = forward)  8 dwStart (frame)
//  12 dwEnd (frame, INCLUSIVE)  16 dwFraction (fraction of a frame / 2^32)
//  20 dwPlayCount
//
// dwEnd is the last frame played, so the exclusive end is dwEnd + 1; the
// spec's dwFraction refines the loop end between that frame and the next.
inline bool parseWav(FILE* f, long fileLen, tzpl_AudioFileMeta* out) {
    bool found = false;
    long pos = 12;  // past "RIFF" size "WAVE"
    unsigned char hdr[8];
    while (pos + 8 <= fileLen) {
        if (!readAt(f, pos, hdr, 8)) break;
        uint32_t chunkLen = rd_u32le(hdr + 4);
        if (memcmp(hdr, "smpl", 4) == 0 && chunkLen >= 36) {
            unsigned char body[36];
            if (!readAt(f, pos + 8, body, 36)) break;
            uint32_t unityNote = rd_u32le(body + 12);
            uint32_t pitchFrac = rd_u32le(body + 16);
            uint32_t numLoops = rd_u32le(body + 28);
            if (unityNote <= 127) {
                out->rootKey = (float)((double)unityNote
                                       + (double)pitchFrac / 4294967296.0);
                found = true;
            }
            // First forward loop wins (v1 reads sustain loops only).
            for (uint32_t li = 0; li < numLoops; ++li) {
                long lpos = pos + 8 + 36 + (long)li * 24;
                unsigned char loop[24];
                if ((long)(36 + (li + 1) * 24) > (long)chunkLen
                    || !readAt(f, lpos, loop, 24)) break;
                uint32_t type = rd_u32le(loop + 4);
                if (type != 0) continue;
                uint32_t start = rd_u32le(loop + 8);
                uint32_t end = rd_u32le(loop + 12);
                uint32_t frac = rd_u32le(loop + 16);
                out->loopStart = (double)start;
                out->loopEnd = (double)end + 1.0 + (double)frac / 4294967296.0;
                found = true;
                break;
            }
            break;  // one smpl chunk per file
        }
        pos += 8 + (long)chunkLen + (chunkLen & 1);  // chunks are word-aligned
    }
    return found;
}

// --- AIFF / AIFC ---------------------------------------------------------
//
// INST chunk (20 bytes, big-endian):
//   0 baseNote (i8)  1 detune (i8, cents)  2 lowNote  3 highNote
//   4 lowVelocity  5 highVelocity  6 gain (i16)
//   8 sustainLoop {playMode u16 (1 = forward), beginLoop u16, endLoop u16}
//  14 releaseLoop {playMode u16, beginLoop u16, endLoop u16}
// beginLoop/endLoop are MARK chunk marker ids; a marker's position is the
// frame BEFORE which the marker sits, so begin..end plays frames
// [begin, end) -- end is already exclusive.
inline bool parseAiff(FILE* f, long fileLen, tzpl_AudioFileMeta* out) {
    long instPos = -1;
    long markPos = -1;
    uint32_t markLen = 0;
    long pos = 12;  // past "FORM" size "AIFF"/"AIFC"
    unsigned char hdr[8];
    while (pos + 8 <= fileLen) {
        if (!readAt(f, pos, hdr, 8)) break;
        uint32_t chunkLen = rd_u32be(hdr + 4);
        if (memcmp(hdr, "INST", 4) == 0 && chunkLen >= 20) instPos = pos + 8;
        if (memcmp(hdr, "MARK", 4) == 0 && chunkLen >= 2) {
            markPos = pos + 8;
            markLen = chunkLen;
        }
        pos += 8 + (long)chunkLen + (chunkLen & 1);
    }
    if (instPos < 0) return false;

    unsigned char inst[20];
    if (!readAt(f, instPos, inst, 20)) return false;
    int8_t baseNote = (int8_t)inst[0];
    int8_t detune = (int8_t)inst[1];
    bool found = false;
    if (baseNote >= 0) {
        out->rootKey = (float)((double)baseNote + (double)detune / 100.0);
        found = true;
    }

    uint16_t playMode = rd_u16be(inst + 8);
    uint16_t beginId = rd_u16be(inst + 10);
    uint16_t endId = rd_u16be(inst + 12);
    if (playMode != 1 || markPos < 0) return found;  // forward loops only

    // Resolve the two marker ids. Markers are variable length: id (u16),
    // position (u32), name (pascal string, padded to even length).
    double beginFrame = -1.0, endFrame = -1.0;
    unsigned char mhdr[2];
    if (!readAt(f, markPos, mhdr, 2)) return found;
    uint16_t numMarkers = rd_u16be(mhdr);
    long mpos = markPos + 2;
    for (uint16_t mi = 0; mi < numMarkers; ++mi) {
        unsigned char m[7];
        if (mpos + 7 > markPos + (long)markLen || !readAt(f, mpos, m, 7)) break;
        uint16_t id = rd_u16be(m);
        uint32_t frame = rd_u32be(m + 2);
        if (id == beginId) beginFrame = (double)frame;
        if (id == endId) endFrame = (double)frame;
        uint32_t nameLen = m[6];
        mpos += 6 + 1 + (long)nameLen + ((nameLen & 1) ? 0 : 1);
    }
    if (beginFrame >= 0.0 && endFrame > beginFrame) {
        out->loopStart = beginFrame;
        out->loopEnd = endFrame;
        found = true;
    }
    return found;
}

} // namespace tzpl_meta_detail

// Reads instrument metadata from a WAV or AIFF/AIFC file. Returns true when
// at least one field was found; `out` fields not present in the file keep
// their "absent" defaults. Any other container returns false.
inline bool tzpl_readAudioFileMeta(char const* path, tzpl_AudioFileMeta* out) {
    using namespace tzpl_meta_detail;
    *out = tzpl_AudioFileMeta{};
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long fileLen = ftell(f);

    unsigned char id[12];
    bool found = false;
    if (fileLen >= 12 && readAt(f, 0, id, 12)) {
        if (memcmp(id, "RIFF", 4) == 0 && memcmp(id + 8, "WAVE", 4) == 0) {
            found = parseWav(f, fileLen, out);
        } else if (memcmp(id, "FORM", 4) == 0
                   && (memcmp(id + 8, "AIFF", 4) == 0
                       || memcmp(id + 8, "AIFC", 4) == 0)) {
            found = parseAiff(f, fileLen, out);
        }
    }
    fclose(f);
    return found;
}
