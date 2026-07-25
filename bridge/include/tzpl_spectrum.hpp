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
//  tzpl_spectrum.hpp
//  bridge
//
//  Host-side spectrum analysis for Spectrum widgets: window, real FFT, and
//  magnitude-in-dB with a per-tick fall.
//
//  Analysis runs in the control dispatcher's fixed-rate tick, NOT in paint():
//  the decay needs a known tick rate, and repaints happen at whatever rate the
//  toolkit decides (resize, expose, occlusion). Renderers only draw the
//  already-computed bins.
//
//  Not thread-safe: one SpectrumEngine per dispatcher, used from that
//  dispatcher's thread only. FFT setups are cached BY SIZE, so any number of
//  widgets at the same fftSize share one.
//

#ifndef tzpl_spectrum_hpp
#define tzpl_spectrum_hpp

#include <unordered_map>
#include <vector>

namespace synthdef { struct JscsFFTSetup; }

namespace bridge {

class SpectrumEngine {
public:
    SpectrumEngine() = default;
    ~SpectrumEngine();

    SpectrumEngine(SpectrumEngine const&) = delete;
    SpectrumEngine& operator=(SpectrumEngine const&) = delete;

    // Number of magnitude bins analyze() writes for a given size: fftSize/2+1
    // (DC through Nyquist inclusive).
    static int numBins(int fftSize) { return fftSize / 2 + 1; }

    // Analyze the newest `fftSize` frames of `ring` (interleaved, `chans`
    // channels). `ch` selects a channel, or < 0 for the mean of all.
    //
    // Writes numBins(fftSize) magnitudes in dBFS into `out`, where a
    // full-scale sine reads 0 dB. Each call falls every bin by `fallDb`
    // first, then takes the max against the new frame, so peaks decay
    // smoothly instead of flickering. With fewer than fftSize frames
    // available the previous values simply keep decaying.
    void analyze(std::vector<float> const& ring, int chans, int ch,
                 int fftSize, float floorDb, float fallDb,
                 std::vector<float>& out);

private:
    synthdef::JscsFFTSetup* setupFor(int fftSize);

    std::unordered_map<int, synthdef::JscsFFTSetup*> setups_;
    std::unordered_map<int, std::vector<float>> windows_;
    std::vector<float> frame_, spec_;
};

} // namespace bridge

#endif /* tzpl_spectrum_hpp */
