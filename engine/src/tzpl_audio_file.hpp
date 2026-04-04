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
//  tzpl_audio_file.hpp
//  Cross-platform audio file loading into tzpl_Buffer.
//  macOS: ExtAudioFile (AudioToolbox)
//  Linux: libsndfile
//

#pragma once

#include "tzpl_plugin_abi.h"
#include <algorithm>
#include <cstring>

#ifdef __APPLE__

#include <AudioToolbox/AudioToolbox.h>

inline tzpl_Buffer* tzpl_loadAudioFile(const char* path, int channelOffset,
                                        int64_t frameOffset, int64_t numFrames) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, (const UInt8*)path, (CFIndex)strlen(path), false);
    if (!url) return nullptr;

    ExtAudioFileRef audioFile = nullptr;
    OSStatus err = ExtAudioFileOpenURL(url, &audioFile);
    CFRelease(url);
    if (err != noErr || !audioFile) return nullptr;

    // Get file format
    AudioStreamBasicDescription fileFormat = {};
    UInt32 propSize = sizeof(fileFormat);
    err = ExtAudioFileGetProperty(audioFile, kExtAudioFileProperty_FileDataFormat,
                                   &propSize, &fileFormat);
    if (err != noErr) { ExtAudioFileDispose(audioFile); return nullptr; }

    // Get total frames
    SInt64 totalFrames = 0;
    propSize = sizeof(totalFrames);
    err = ExtAudioFileGetProperty(audioFile, kExtAudioFileProperty_FileLengthFrames,
                                   &propSize, &totalFrames);
    if (err != noErr) { ExtAudioFileDispose(audioFile); return nullptr; }

    int fileChans = (int)fileFormat.mChannelsPerFrame;
    int outChans = fileChans - channelOffset;
    if (outChans <= 0) { ExtAudioFileDispose(audioFile); return nullptr; }

    // Set client format to Float64 non-interleaved
    AudioStreamBasicDescription clientFormat = {};
    clientFormat.mSampleRate = fileFormat.mSampleRate;
    clientFormat.mFormatID = kAudioFormatLinearPCM;
    clientFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved;
    clientFormat.mBitsPerChannel = 64;
    clientFormat.mChannelsPerFrame = (UInt32)fileChans;
    clientFormat.mFramesPerPacket = 1;
    clientFormat.mBytesPerFrame = 8;
    clientFormat.mBytesPerPacket = 8;
    err = ExtAudioFileSetProperty(audioFile, kExtAudioFileProperty_ClientDataFormat,
                                   sizeof(clientFormat), &clientFormat);
    if (err != noErr) { ExtAudioFileDispose(audioFile); return nullptr; }

    // Seek to frameOffset
    if (frameOffset > 0) {
        err = ExtAudioFileSeek(audioFile, frameOffset);
        if (err != noErr) { ExtAudioFileDispose(audioFile); return nullptr; }
    }

    int64_t framesToRead = std::min(numFrames, (int64_t)totalFrames - frameOffset);
    if (framesToRead <= 0) { ExtAudioFileDispose(audioFile); return nullptr; }

    tzpl_Buffer* buf = tzpl_createBuffer(outChans, framesToRead);
    if (!buf) { ExtAudioFileDispose(audioFile); return nullptr; }

    // Set up AudioBufferList for non-interleaved reading
    size_t ablSize = sizeof(AudioBufferList) + sizeof(AudioBuffer) * ((size_t)fileChans - 1);
    AudioBufferList* abl = (AudioBufferList*)calloc(1, ablSize);
    abl->mNumberBuffers = (UInt32)fileChans;

    // Point each buffer at the corresponding channel data (skipping channelOffset)
    // For channels before channelOffset, use a temp discard buffer
    double* discard = nullptr;
    if (channelOffset > 0) {
        discard = (double*)calloc((size_t)framesToRead, sizeof(double));
    }

    for (int ch = 0; ch < fileChans; ch++) {
        abl->mBuffers[ch].mNumberChannels = 1;
        abl->mBuffers[ch].mDataByteSize = (UInt32)(framesToRead * sizeof(double));
        if (ch < channelOffset) {
            abl->mBuffers[ch].mData = discard ? discard : buf->data[0]; // discard
        } else if (ch - channelOffset < outChans) {
            abl->mBuffers[ch].mData = buf->data[ch - channelOffset];
        } else {
            abl->mBuffers[ch].mData = discard ? discard : buf->data[0]; // discard extra chans
        }
    }

    UInt32 framesRead = (UInt32)framesToRead;
    err = ExtAudioFileRead(audioFile, &framesRead, abl);

    free(discard);
    free(abl);
    ExtAudioFileDispose(audioFile);

    if (err != noErr) { tzpl_freeBuffer(buf); return nullptr; }

    // Update actual length if fewer frames were read
    if ((int64_t)framesRead < framesToRead) {
        buf->length = (int64_t)framesRead;
    }

    return buf;
}

#else // Linux

#include <sndfile.h>
#include <vector>

inline tzpl_Buffer* tzpl_loadAudioFile(const char* path, int channelOffset,
                                        int64_t frameOffset, int64_t numFrames) {
    SF_INFO sfinfo = {};
    SNDFILE* sf = sf_open(path, SFM_READ, &sfinfo);
    if (!sf) return nullptr;

    int fileChans = sfinfo.channels;
    int64_t totalFrames = sfinfo.frames;
    int outChans = fileChans - channelOffset;
    if (outChans <= 0) { sf_close(sf); return nullptr; }

    if (frameOffset > 0) sf_seek(sf, frameOffset, SEEK_SET);

    int64_t framesToRead = std::min(numFrames, totalFrames - frameOffset);
    if (framesToRead <= 0) { sf_close(sf); return nullptr; }

    tzpl_Buffer* buf = tzpl_createBuffer(outChans, framesToRead);
    if (!buf) { sf_close(sf); return nullptr; }

    // Read interleaved, then de-interleave
    std::vector<double> interleaved((size_t)(framesToRead * fileChans));
    sf_count_t framesRead = sf_readf_double(sf, interleaved.data(), framesToRead);
    sf_close(sf);

    int64_t actualFrames = std::min((int64_t)framesRead, framesToRead);
    for (int ch = 0; ch < outChans; ch++) {
        for (int64_t f = 0; f < actualFrames; f++) {
            buf->data[ch][f] = interleaved[(size_t)(f * fileChans + ch + channelOffset)];
        }
    }

    if (actualFrames < framesToRead) {
        buf->length = actualFrames;
    }

    return buf;
}

#endif // __APPLE__
