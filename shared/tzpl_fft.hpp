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

/*
 *  tzpl_fft.hpp
 *  Cross-platform FFT wrapper for TZPL audio plugins.
 *
 *  Provides real FFT/IFFT in split-complex packed format:
 *    [real[0], real[1], ..., real[N/2-1],  imag[0], imag[1], ..., imag[N/2-1]]
 *  where real[0] = DC component, imag[0] = Nyquist component.
 *
 *  Backend: vDSP (Accelerate) on Apple, PFFFT elsewhere (TODO).
 */

#pragma once

#include <cstdlib>
#include <cmath>
#include <cassert>

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#endif

namespace synthdef {

struct JscsFFTSetup {
    int fftSize;
    int log2n;
#ifdef __APPLE__
    FFTSetup vdspSetup;
#endif
};

inline JscsFFTSetup* tzpl_fft_create(int fftSize) {
    assert(fftSize > 0 && (fftSize & (fftSize - 1)) == 0); // must be power of 2
    auto* s = (JscsFFTSetup*)calloc(1, sizeof(JscsFFTSetup));
    s->fftSize = fftSize;
    s->log2n = 0;
    int tmp = fftSize;
    while (tmp > 1) { tmp >>= 1; s->log2n++; }
#ifdef __APPLE__
    s->vdspSetup = vDSP_create_fftsetup(s->log2n, FFT_RADIX2);
#endif
    return s;
}

inline void tzpl_fft_destroy(JscsFFTSetup* s) {
    if (!s) return;
#ifdef __APPLE__
    vDSP_destroy_fftsetup(s->vdspSetup);
#endif
    free(s);
}

// Forward real FFT: time-domain (fftSize floats) -> split-complex packed (fftSize floats)
// The input buffer is not modified. Output is written to 'output'.
// Output layout: [real[0..N/2-1], imag[0..N/2-1]]
//   real[0] = DC, imag[0] = Nyquist
inline void tzpl_fft_forward(JscsFFTSetup* s, const float* input, float* output) {
    int N = s->fftSize;
    int halfN = N / 2;

#ifdef __APPLE__
    // Pack real input into split-complex form for vDSP:
    // realp[k] = input[2k], imagp[k] = input[2k+1]
    float* realp = output;
    float* imagp = output + halfN;
    for (int k = 0; k < halfN; ++k) {
        realp[k] = input[2 * k];
        imagp[k] = input[2 * k + 1];
    }

    DSPSplitComplex sc = { realp, imagp };
    vDSP_fft_zrip(s->vdspSetup, &sc, 1, s->log2n, FFT_FORWARD);

    // vDSP forward FFT returns 2x the standard DFT; scale by 0.5
    float scale = 0.5f;
    vDSP_vsmul(realp, 1, &scale, realp, 1, halfN);
    vDSP_vsmul(imagp, 1, &scale, imagp, 1, halfN);
#else
    // TODO: PFFFT implementation
    (void)input; (void)output;
#endif
}

// Inverse real FFT: split-complex packed (fftSize floats) -> time-domain (fftSize floats)
// The input buffer is not modified. Output is written to 'output'.
inline void tzpl_fft_inverse(JscsFFTSetup* s, const float* input, float* output) {
    int N = s->fftSize;
    int halfN = N / 2;

#ifdef __APPLE__
    // Copy input to output buffer (vDSP works in-place on split complex)
    float* realp = output;
    float* imagp = output + halfN;
    for (int k = 0; k < halfN; ++k) {
        realp[k] = input[k];
        imagp[k] = input[halfN + k];
    }

    DSPSplitComplex sc = { realp, imagp };
    vDSP_fft_zrip(s->vdspSetup, &sc, 1, s->log2n, FFT_INVERSE);

    // Unpack split-complex to interleaved real output
    // After inverse, realp[k] and imagp[k] represent the even/odd real samples
    float tmpR[halfN], tmpI[halfN];
    for (int k = 0; k < halfN; ++k) {
        tmpR[k] = realp[k];
        tmpI[k] = imagp[k];
    }
    for (int k = 0; k < halfN; ++k) {
        output[2 * k]     = tmpR[k];
        output[2 * k + 1] = tmpI[k];
    }

    // vDSP inverse FFT returns (N/2)x the standard IDFT; scale by 1/(N/2)
    float scale = 1.0f / (float)halfN;
    vDSP_vsmul(output, 1, &scale, output, 1, N);
#else
    // TODO: PFFFT implementation
    (void)input; (void)output;
#endif
}

// Generate Hann window coefficients
inline void tzpl_window_hann(float* buf, int size) {
    for (int i = 0; i < size; ++i) {
        buf[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)size));
    }
}

// Generate sqrt-Hann window coefficients (for use as both analysis and synthesis window)
inline void tzpl_window_sqrt_hann(float* buf, int size) {
    for (int i = 0; i < size; ++i) {
        buf[i] = sqrtf(0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)size)));
    }
}

} // namespace synthdef
