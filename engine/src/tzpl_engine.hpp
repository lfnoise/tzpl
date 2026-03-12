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
//  tzpl_engine.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_engine_hpp
#define tzpl_engine_hpp

#include "tzpl_client_interface.hpp"
#include "tzpl_silo.hpp"
#include <mutex>
#include <vector>
#include <thread>

namespace engine {

struct Node;

//=============================================================================================
#pragma mark SAFETY LIMITER

struct SafetyLimiter
{
	f32 nextGain = 1.f;
	f32 peakLimit = 1.f;
	f32 prevMaxPeak = 1.f;
	
	f32 prevCombinedGain = 1.f;
 	
	f32 recover = 0.04;
	
	int bufFrames;
	int numChannels;
	int samples;
	
	int holdCount = 0;
	int holdDur;
	
	std::vector<f32> prevBuf;
	
	SafetyLimiter(int inBufFrames, int inNumChannels, int holdFrames)
		: bufFrames(inBufFrames), numChannels(inNumChannels),
		samples(bufFrames * numChannels),
		holdDur(holdFrames),
		prevBuf(samples)
	{}
	
	f32 maxAbsPeak(f32* buf) {
		f32 m = 0.f;
		for (int i = 0; i < samples; ++i) {
			m = std::max(m, std::abs(buf[i]));
		}
		return m;
	}
	
	void calcGain(f32 nextMaxPeak) {
		//printf(">g %f  %f %f  %f %f\n", gain, prevGain, nextGain, prevMaxPeak, nextMaxPeak);
		f32 prevGain = nextGain;
		nextGain = peakLimit / std::max({prevMaxPeak, nextMaxPeak, peakLimit});
		if (nextGain < prevGain) {
			//printf("hold %f\n", nextGain);
			holdCount = holdDur;
		} else if (holdCount > 0) {
			nextGain = prevGain;
			--holdCount;
			//if (holdCount == 0) printf("off hold %f\n", nextGain);
		} else {
			if (nextGain == prevGain) {
				nextGain = prevGain;
			} else {
				f32 releaseGain = std::min(1.f, prevGain + recover);
				if (nextGain < releaseGain) {
					nextGain = prevGain;
					holdCount = holdDur;
					//printf("hold release %f\n", nextGain);
				} else {
					nextGain = releaseGain;
					//printf("release %f\n", nextGain);
				}
			}
		}
		assert(0.f <= nextGain && nextGain <= 1.f);
		prevMaxPeak = nextMaxPeak;
	}
	
	void process(f32* nextBuf, Enable enable, f32 postGain) {
		zap(nextBuf); // eliminate NaNs and denormals and clamp large values.

//		static bool wasEnable = false;
		if (enable == kOn) {
			calcGain(maxAbsPeak(nextBuf));
			f32 nextCombinedGain = postGain * nextGain;
//			if (!wasEnable) {
//				printf("g %f %f %f  pk %f h %d\n",
//					postGain, nextGain, nextCombinedGain, prevMaxPeak, holdCount);
//			}
			if (nextCombinedGain == prevCombinedGain) {
				if (nextCombinedGain != 1.f) {
					for (int i = 0; i < samples; ++i) {
						prevBuf[i] *= nextCombinedGain;
					}
				}
			} else {
				f32 slope = (nextCombinedGain - prevCombinedGain) / (f32)bufFrames;
				f32 gain = prevCombinedGain;
				for (int i = 0, k = 0; i < bufFrames; ++i) {
					for (int j = 0; j < numChannels; ++j, ++k) {
						prevBuf[k] *= gain;
					}
					gain += slope;
				}
				prevCombinedGain = nextCombinedGain;
			}
			std::swap_ranges(&prevBuf[0], &prevBuf[samples], &nextBuf[0]);
		} else {
            f32 nextCombinedGain = postGain;
			if (nextCombinedGain == prevCombinedGain) {
				if (nextCombinedGain != 1.f) {
					for (int i = 0; i < samples; ++i) {
						nextBuf[i] *= nextCombinedGain;
					}
				}
			} else {
				f32 slope = (nextCombinedGain - prevCombinedGain) / (f32)bufFrames;
				f32 gain = prevCombinedGain;
				for (int i = 0, k = 0; i < bufFrames; ++i) {
					for (int j = 0; j < numChannels; ++j, ++k) {
						nextBuf[k] *= gain;
					}
					gain += slope;
				}
				prevCombinedGain = nextCombinedGain;
			}
        }      
//		wasEnable = enable;
	}
	
	void zap(f32* buf) {
		for (int i = 0; i < samples; ++i) {
			f32 x = buf[i];
			f32 absx = fabs(x);
			x = absx > 1e-10f && absx < 1e10f ? x : 0.f;  // eliminate denormals, infs and NaNs.
			// peak limiter will take care of |x| > 1.
			buf[i] = x;
		}
	}
};


//=============================================================================================
#pragma mark ENGINE

enum class AudioState {
    off,
    initted,
    running,
};

struct Engine
{
    std::mutex nrt_lock_;
	std::atomic_int runSilos_ = 1;
    
	std::vector<Silo> silos_;
	std::vector<NodeDef*> defs_;
	std::unique_ptr<RtAudio> rtaudio_;
		
	AudioState audioState_ = AudioState::off;
	
	f64 anchorStreamTime_ = 0.;
	i64 anchorSampleTime_ = 0;
	bool runBackgroundThreads_ = true;
	std::thread nrt_cmd_thread_;
	std::thread dead_node_thread_;

	std::unique_ptr<SafetyLimiter> safetyLimiter_;
	Enable enableSafetyLimiter_ = kOn;
	f32 masterGain_ = 1.f;
	f32 muteGain_ = 1.f;
	AudioStreamParameters streamParams_;
	
	f32 const* in_ = nullptr;
	f32* out_ = nullptr;

	// Separate input device support
	std::unique_ptr<RtAudio> inputRtaudio_;  // non-null when using a separate input device
	f32* inputStagingBuf_ = nullptr;          // intermediate buffer for separate input device

    Engine(EngineConfig const& config, AudioStreamParameters& streamParams);
    ~Engine();

	void defOutputNode(int numChannels);
	void defInputNode(int inputChannels);
	void defXFaderNode();
 
    bool isAudioInitialized() const { return audioState_ >= AudioState::initted; }
    bool isAudioRunning() const { return audioState_ == AudioState::running; }

	f64 getStreamTime();
	i64 streamTimeToSampleTime(f64 streamTime);
	
    // real time methods
	
	int noteOn(Node* node, int noteID, f32 pch, f32 vel);
	int noteOff(Node* node, int noteID, f32 vel);
	int noteControl(Node* node, int noteID, i64 controlID, f32 value);
	int noteControl(Node* node, int noteID, i64 controlID, int byteSize, void* values);

	static void processNRTCommands(Engine* e);
	static void processDeadNodes(Engine* e);
};

}

#endif /* tzpl_engine_hpp */
