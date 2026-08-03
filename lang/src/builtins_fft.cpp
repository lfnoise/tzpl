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
//  builtins_fft.cpp
//  lang
//
//  Real FFT builtins over [Float], wrapping the double-precision API in
//  shared/tzpl_fft.hpp. Packed split-complex layout, identical to the
//  header's convention:
//    [re[0], re[1], ..., re[N/2-1], im[0], im[1], ..., im[N/2-1]]
//  where re[0] = DC and im[0] = Nyquist. ifft is the exact inverse of fft:
//  ifft(fft(x)) unwrap == x.
//
//  Both builtins return Option<[Float]>: .None when the input length is not
//  a power of two >= 4. Registered pure=true (deterministic; the constant
//  folder only folds scalar args so it can never fire on these) and
//  rtSafe=false: the setup cache below uses the system allocator and a
//  mutex, so the type checker rejects fft/ifft in RT compilations. A
//  [Complex] overload can be layered on later in pure .x if wanted; the
//  packed-Nyquist convention makes a native complex API lossy or awkward.
//

#include "builtins_internal.hpp"
#include "vm.hpp"
#include "tzpl_fft.hpp"

#include <map>
#include <mutex>

namespace ts {

// ---------------------------------------------------------------------------
// Setup cache: system-allocating, mutex-guarded, process-lifetime. NRT-only
// (rtSafe=false), mirroring SpectrumEngine::setupFor in tzpl_spectrum.cpp.
// ---------------------------------------------------------------------------

static synthdef::JscsFFTSetupD* fftSetupFor(int n) {
    static std::mutex mtx;
    static std::map<int, synthdef::JscsFFTSetupD*> setups;
    std::lock_guard<std::mutex> lock(mtx);
    auto it = setups.find(n);
    if (it != setups.end()) return it->second;
    auto* s = synthdef::tzpl_fft_create_d(n);
    setups.emplace(n, s);
    return s;
}

static void fftCommon(VM& vm, u16 dst, u16 ab, bool inverse) {
    auto* src = static_cast<PodArray<f64>*>(vm.reg(ab).o);
    size_t n = src->v.size();
    if (n < 4 || (n & (n - 1)) != 0) { vm.reg(dst).o = nullptr; return; } // None
    GCKeepAliveScope keep(vm, src);
    auto* out = new PodArray<f64>(vm.arrayType(vm.floatType()));
    out->v.resize(n);
    auto* setup = fftSetupFor((int)n);
    if (inverse) synthdef::tzpl_fft_inverse_d(setup, src->v.data(), out->v.data());
    else         synthdef::tzpl_fft_forward_d(setup, src->v.data(), out->v.data());
    vm.reg(dst).o = out; // Some
}

// fft(x [Float]) Option<[Float]>
static void builtin_fft_array(VM& vm, u16 dst, u16, u16 ab) {
    fftCommon(vm, dst, ab, /*inverse=*/false);
}

// ifft(x [Float]) Option<[Float]>
static void builtin_ifft_array(VM& vm, u16 dst, u16, u16 ab) {
    fftCommon(vm, dst, ab, /*inverse=*/true);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerFftBuiltins(Compiler& compiler, FuncMap& functions) {
    Type* Float    = compiler.floatType();
    Type* ArrFloat = compiler.arrayType(Float);
    Type* OptArrF  = compiler.optionType(ArrFloat);

    registerOne(compiler, functions, "fft",  OptArrF, {ArrFloat}, builtin_fft_array,  /*pure=*/true, /*rtSafe=*/false);
    registerOne(compiler, functions, "ifft", OptArrF, {ArrFloat}, builtin_ifft_array, /*pure=*/true, /*rtSafe=*/false);
}

} // namespace ts
