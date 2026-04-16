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
//  tzpl_audio_file_writer.cpp
//  audio engine
//

#include "tzpl_audio_file_writer.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace engine {

struct WavWriter {
    FILE* fp = nullptr;
    int sampleRate = 0;
    int numChannels = 0;
    uint64_t framesWritten = 0;
};

// WAV (WAVE_FORMAT_IEEE_FLOAT) header. Sizes are back-filled on close.
// Layout:
//   "RIFF" <riffSize:u32> "WAVE"
//   "fmt " <16:u32> <fmt:u16=3> <ch:u16> <sr:u32> <byteRate:u32> <blockAlign:u16> <bitsPerSample:u16=32>
//   "data" <dataSize:u32> <samples...>
static constexpr int kHeaderBytes = 44;

static bool writeAll(FILE* fp, void const* data, size_t bytes) {
    return fwrite(data, 1, bytes, fp) == bytes;
}

static void writeU16LE(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v); p[1] = uint8_t(v >> 8);
}

static void writeU32LE(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);       p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}

static bool writePlaceholderHeader(WavWriter* w) {
    uint8_t hdr[kHeaderBytes];
    memset(hdr, 0, sizeof(hdr));

    int bitsPerSample = 32;
    int blockAlign = w->numChannels * (bitsPerSample / 8);
    int byteRate = w->sampleRate * blockAlign;

    memcpy(hdr + 0, "RIFF", 4);
    writeU32LE(hdr + 4, 0); // riff size, back-filled on close
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    writeU32LE(hdr + 16, 16);                 // fmt chunk size
    writeU16LE(hdr + 20, 3);                  // WAVE_FORMAT_IEEE_FLOAT
    writeU16LE(hdr + 22, uint16_t(w->numChannels));
    writeU32LE(hdr + 24, uint32_t(w->sampleRate));
    writeU32LE(hdr + 28, uint32_t(byteRate));
    writeU16LE(hdr + 32, uint16_t(blockAlign));
    writeU16LE(hdr + 34, uint16_t(bitsPerSample));
    memcpy(hdr + 36, "data", 4);
    writeU32LE(hdr + 40, 0); // data size, back-filled on close

    return writeAll(w->fp, hdr, sizeof(hdr));
}

WavWriter* wavOpen(char const* path, int sampleRate, int numChannels) {
    if (!path || sampleRate <= 0 || numChannels <= 0) {
        fprintf(stderr, "wavOpen: invalid arguments\n");
        return nullptr;
    }
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "wavOpen: cannot open '%s' for writing\n", path);
        return nullptr;
    }
    auto* w = new WavWriter();
    w->fp = fp;
    w->sampleRate = sampleRate;
    w->numChannels = numChannels;
    w->framesWritten = 0;
    if (!writePlaceholderHeader(w)) {
        fprintf(stderr, "wavOpen: failed writing header to '%s'\n", path);
        fclose(fp);
        delete w;
        return nullptr;
    }
    return w;
}

bool wavWrite(WavWriter* w, f32 const* interleaved, int numFrames) {
    if (!w || !interleaved || numFrames <= 0) return numFrames == 0;
    size_t bytes = size_t(numFrames) * size_t(w->numChannels) * sizeof(f32);
    if (!writeAll(w->fp, interleaved, bytes)) {
        fprintf(stderr, "wavWrite: short write\n");
        return false;
    }
    w->framesWritten += uint64_t(numFrames);
    return true;
}

void wavClose(WavWriter* w) {
    if (!w) return;
    if (w->fp) {
        // Back-fill sizes.
        uint64_t dataBytes = w->framesWritten * uint64_t(w->numChannels) * sizeof(f32);
        // WAV size fields are 32-bit; clamp if file exceeded 4 GiB.
        uint32_t dataSize32 = dataBytes > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32_t(dataBytes);
        uint64_t riffBytes = dataBytes + (kHeaderBytes - 8);
        uint32_t riffSize32 = riffBytes > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32_t(riffBytes);

        uint8_t buf[4];
        if (fseek(w->fp, 4, SEEK_SET) == 0) {
            writeU32LE(buf, riffSize32);
            fwrite(buf, 1, 4, w->fp);
        }
        if (fseek(w->fp, 40, SEEK_SET) == 0) {
            writeU32LE(buf, dataSize32);
            fwrite(buf, 1, 4, w->fp);
        }
        fclose(w->fp);
    }
    delete w;
}

}
