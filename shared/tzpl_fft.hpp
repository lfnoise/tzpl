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
 *  Backend: vDSP (Accelerate) on Apple, a portable radix-2 fallback
 *  elsewhere. Both produce the identical packed layout described above.
 */

#pragma once

#include <cstdlib>
#include <cmath>
#include <cassert>

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#else
#include <vector>
#endif

namespace synthdef {

struct JscsFFTSetup {
    int fftSize;
    int log2n;
#ifdef __APPLE__
    FFTSetup vdspSetup;
#else
    // Precomputed bit-reversal permutation and twiddles for the portable path.
    std::vector<int> rev;
    std::vector<float> cosTab, sinTab;
    // Interleaved complex scratch, so forward/inverse allocate nothing.
    std::vector<float> re, im;
#endif
};

inline JscsFFTSetup* tzpl_fft_create(int fftSize) {
    assert(fftSize > 0 && (fftSize & (fftSize - 1)) == 0); // must be power of 2
    auto* s = new JscsFFTSetup();
    s->fftSize = fftSize;
    s->log2n = 0;
    int tmp = fftSize;
    while (tmp > 1) { tmp >>= 1; s->log2n++; }
#ifdef __APPLE__
    s->vdspSetup = vDSP_create_fftsetup(s->log2n, FFT_RADIX2);
#else
    // The real-input transform runs as a complex FFT of length N/2, so all
    // tables are sized for that.
    int const halfN = fftSize / 2;
    s->rev.resize((size_t)halfN);
    int bits = s->log2n - 1;
    for (int i = 0; i < halfN; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        s->rev[(size_t)i] = r;
    }
    s->cosTab.resize((size_t)halfN);
    s->sinTab.resize((size_t)halfN);
    for (int k = 0; k < halfN; ++k) {
        double a = -2.0 * M_PI * (double)k / (double)halfN;
        s->cosTab[(size_t)k] = (float)std::cos(a);
        s->sinTab[(size_t)k] = (float)std::sin(a);
    }
    s->re.resize((size_t)halfN);
    s->im.resize((size_t)halfN);
#endif
    return s;
}

inline void tzpl_fft_destroy(JscsFFTSetup* s) {
    if (!s) return;
#ifdef __APPLE__
    vDSP_destroy_fftsetup(s->vdspSetup);
#endif
    delete s;
}

#ifndef __APPLE__
// In-place complex FFT of length halfN over s->re / s->im. `inverse` conjugates
// the twiddles; no normalization is applied here (callers scale to match the
// vDSP conventions documented on each entry point).
inline void tzpl_fft_complex_(JscsFFTSetup* s, bool inverse) {
    int const n = s->fftSize / 2;
    float* re = s->re.data();
    float* im = s->im.data();

    for (int i = 0; i < n; ++i) {
        int j = s->rev[(size_t)i];
        if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        int const step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < len / 2; ++k) {
                float wr = s->cosTab[(size_t)(k * step)];
                float wi = s->sinTab[(size_t)(k * step)];
                if (inverse) wi = -wi;
                int a = i + k, b = a + len / 2;
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
            }
        }
    }
}
#endif

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
    // Real-input FFT via a half-length complex FFT: pack the even samples as
    // the real part and the odd samples as the imaginary part, transform, then
    // untangle. Produces exactly the layout vDSP does above, including the
    // convention that imag[0] carries Nyquist rather than the (always zero)
    // DC imaginary part.
    for (int k = 0; k < halfN; ++k) {
        s->re[(size_t)k] = input[2 * k];
        s->im[(size_t)k] = input[2 * k + 1];
    }
    tzpl_fft_complex_(s, /*inverse=*/false);

    float* realp = output;
    float* imagp = output + halfN;
    float const* fr = s->re.data();
    float const* fi = s->im.data();

    // Bin 0 and Nyquist are purely real and share slot 0.
    realp[0] = fr[0] + fi[0];
    imagp[0] = fr[0] - fi[0];

    for (int k = 1; k < halfN; ++k) {
        int const nk = halfN - k;
        // Even/odd decomposition of the packed spectrum.
        float er = 0.5f * (fr[k] + fr[nk]);
        float ei = 0.5f * (fi[k] - fi[nk]);
        float or_ = 0.5f * (fi[k] + fi[nk]);
        float oi = -0.5f * (fr[k] - fr[nk]);
        double a = -2.0 * M_PI * (double)k / (double)N;
        float wr = (float)std::cos(a), wi = (float)std::sin(a);
        realp[k] = er + (or_ * wr - oi * wi);
        imagp[k] = ei + (or_ * wi + oi * wr);
    }
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
    // Inverse of the packing above: rebuild the half-length complex spectrum,
    // transform back, and de-interleave. Scaled to match vDSP's convention
    // (its inverse returns halfN x the standard IDFT, which the shared caller
    // code then divides out) so both backends behave identically.
    float const* realp = input;
    float const* imagp = input + halfN;

    s->re[0] = realp[0] + imagp[0];
    s->im[0] = realp[0] - imagp[0];
    for (int k = 1; k < halfN; ++k) {
        int const nk = halfN - k;
        float er = 0.5f * (realp[k] + realp[nk]);
        float ei = 0.5f * (imagp[k] - imagp[nk]);
        float xr = 0.5f * (realp[k] - realp[nk]);
        float xi = 0.5f * (imagp[k] + imagp[nk]);
        double a = 2.0 * M_PI * (double)k / (double)N;
        float wr = (float)std::cos(a), wi = (float)std::sin(a);
        float or_ = xr * wr - xi * wi;
        float oi = xr * wi + xi * wr;
        s->re[(size_t)k] = er - oi;
        s->im[(size_t)k] = ei + or_;
    }
    tzpl_fft_complex_(s, /*inverse=*/true);

    for (int k = 0; k < halfN; ++k) {
        output[2 * k]     = s->re[(size_t)k];
        output[2 * k + 1] = s->im[(size_t)k];
    }
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
