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

#include "tzpl_audio_backend.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_engine_stats.hpp"
#include "tzpl_silo.hpp"
#include "tzpl_tap.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
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
			// When the limiter is active (nextGain < 1), master gain can reduce
			// the effective gain further but never increase it above the limiter's
			// gain.  When the limiter is not active, master gain applies freely.
			f32 effectivePostGain = nextGain < 1.f ? std::min(postGain, 1.f) : postGain;
			f32 nextCombinedGain = effectivePostGain * nextGain;
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
	int numTempoClocks_ = 1; // number of TempoClock slots per silo
	std::vector<NodeDef*> defs_;

	// Signal tap registry (meters/scopes), keyed by caller-chosen tapID.
	// Guarded by nrt_lock_. Slots are created at bundle submit, installed on
	// a silo's RT tap table by TapOutletCmd, and freed by UntapCmd::doNRT
	// (which runs under nrt_lock_ in the NRT drain loop).
	std::unordered_map<i64, std::unique_ptr<TapSlot>> taps_;
	// Source of unique tapIDs. Callers may still choose their own IDs, but
	// every in-tree client (ui widgets, the graph view) draws from here so
	// two independent features can never collide on one ID.
	std::atomic<i64> nextTapID_{1};

	// Guards every Silo's GraphShadow. Separate from nrt_lock_ because a
	// ShadowCommitCmd's doNRT can run inline (audio stopped) while sched()
	// holds nrt_lock_. Lock order: shadowMtx_ may be acquired while holding
	// nrt_lock_, never the reverse.
	std::mutex shadowMtx_;
	// Bumped (release) whenever a shadow commit lands or a def is
	// (re)registered. GUI pollers read this lock-free and re-snapshot only
	// on change. Starts at 1 so a poller initialized with lastGen = 0
	// unconditionally takes its first snapshot.
	std::atomic<u64> graphGeneration_{1};
	// Audio device backend (RtAudio, JUCE, ...). Null in NRT mode.
	std::unique_ptr<AudioBackend> backend_;

	AudioState audioState_ = AudioState::off;

	f64 anchorStreamTime_ = 0.;
	i64 anchorSampleTime_ = 0;
	// NRT (offline) rendering mode. When true: no RtAudio device is opened,
	// background NRT/dead-node threads are no-ops, worker silos skip the
	// SCHED_RR priority bump, and renderNRTBlock() drives processing.
	// Each NRT render owns its own Engine instance (created via
	// newEngineNRT). The live engine is a separate Engine instance with
	// nrtMode_ = false.
	bool nrtMode_ = false;
	bool runBackgroundThreads_ = true;
	std::thread nrt_cmd_thread_;
	std::thread dead_node_thread_;

	// Master-bus taps (meters/scopes on the post-limiter output). Dense
	// prefix, touched only by the audio thread -- installed and removed by
	// TapMasterCmd/UntapCmd running on silo 0, accumulated once per block by
	// processMasterTaps. The TapSlots are owned by taps_ like any other.
	static constexpr int kMaxMasterTaps = 8;
	TapSlot* rt_masterTaps_[kMaxMasterTaps] = {};
	int numMasterTaps_ = 0;

	tzpl_SErr installMasterTap(TapSlot* slot);
	void removeMasterTap(i64 tapID);
	// RT thread only, and only silo 0's -- see Silo::rt_findTap.
	TapSlot* rt_findMasterTap(i64 tapID);
	// Block-rate peak/rms accumulation and scope capture on the master bus.
	void processMasterTaps(f32 const* out, int frames, int outChans);

	// ---- Metering & monitoring (see tzpl_engine_stats.hpp). ----
	// Master output level, measured post-limiter/post-gain in
	// processAudioBlock. Always on: no tap or widget required.
	MasterMeter masterMeter_;
	// Engine-wide block timing and dropout counters.
	EngineStatsRT stats_;
	// The device's own xrun counter is free-running and belongs to the driver,
	// so resetEngineStats cannot zero it -- it records the current value here
	// instead, and getEngineStats reports the difference. Guarded by
	// nrt_lock_, like everything else the stats path touches.
	u64 deviceXrunBase_ = 0;
	// Cleared to skip all timing instrumentation (offline renders, profiling).
	std::atomic<bool> statsEnabled_{true};

	std::unique_ptr<SafetyLimiter> safetyLimiter_;
	Enable enableSafetyLimiter_ = kOn;
	f32 masterGain_ = 1.f;
	f32 muteGain_ = 1.f;
	AudioStreamParameters streamParams_;
	
	f32 const* in_ = nullptr;
	f32* out_ = nullptr;

	// Set by a backend listener when the device's nominal sample rate is
	// changed externally; processNRTCommands stops the stream on the next tick.
	std::atomic<bool> sampleRateChanged_{false};

    Engine(EngineConfig const& config, AudioStreamParameters& streamParams,
           std::unique_ptr<AudioBackend> backend);
    Engine(EngineConfig const& config, AudioStreamParameters& streamParams, bool nrt);
    ~Engine();

    // Internal helper: starts worker threads and runs initial setup. Called by both ctors.
    void postInit();
    // Internal helper: sizes the master meter's publish window / fall rate and
    // the per-block time budget from streamParams_. Called once the stream
    // format is final (after the backend has negotiated it).
    void configureStats();
    // Internal helper: drains NRT command + dead-node queues. In NRT mode, the
    // background threads are no-ops and the renderer calls this between blocks.
    void drainNRTQueues();

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
