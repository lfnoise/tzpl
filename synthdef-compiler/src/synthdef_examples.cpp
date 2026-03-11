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
//  synthdef_examples.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/24/24.
//

#include "synthdef_examples.hpp"

namespace synthdef {


//    (= dustone (fn () (do
//        (= amp (* .2 (decay2 (dust 4 [2]) .04 .3)))
//        (= out (* amp (fsinxosc 800 0)))
//        (jackout out))))
//
//
//    (= dust1 (fn () (do
//        (= f (biexp (sinosc .3 0) 100 6000))
//        (= out (* .2 (velvet f [2])))
//        (jackout out))))
//        
//
//    (= sahtone1 (fn () (do
//        (= freq (lag (biexp (sah (coin (/ 3 (fs)) 1) (white [2])) 120  800) .04))
//        (= in (* .2 (lftri freq 0)))
//        (= out (combn in .2 2))
//        (jackout out))))
//        
//    (= sahtone2 (fn () (do
//        (jackout out))))
//    

#if 1
void dustone() {
    S amp = .2 * decay2(dust(4, 2), .04, .3);
    S out = amp * fsinxosc(800);
    outlet(out);
}

void dust1() {
    S f = biexp(sinosc(.3), 100, 6000);
    S out = .2 * velvet(f, 2);
    outlet(out);
}


void sahtone1() {
    S freq = lag(biexp(sah(white(2), coin(3*sd(), 1)), 120, 800), .04);
    S in = .2 * lftri(freq);
    S out = combn(in, .2, 2);
    outlet(out);
}


void sahtone2() {
    S freq = lag(biexp(sah(white(2), lfimp(3)), 120, 800), .04);
    S in = .2 * tanh(fsinxosc(freq));
    S out = combn(in, .2, 2);
    outlet(out);
}


//void pan1_test() {
//    S out = pan(.9 * lftri(.2), .4 * white(1));
//    outlet(out);
//}

void violet_tremolo() {
    S out = tremolo(.2 * violet(2), 2, .5, vec(0, .25));
    outlet(out);
}


void bubbles() {
    S freq = nnhz(lfsaw(.4) * 24 + lfsaw(vec(8,7.23)) * 3 + 81);
    S out = .04 * fsinosc(freq);
    out = combn(fadein(out, .1), .2, 4);
    outlet(out);
}

void bubbles_lite() {
    S freq = nnhz(lfsaw(.4) * 24 + lfsaw(vec(8,7.23)) * 3 + 81);
    S out = .2 * fsinosc(freq);
    outlet(out);
}


void pause_bubbles() {
    S gate = sinosc(2);
    S out = pause(gate, [](){   
        S freq = nnhz(lfsaw(.4) * 24 + lfsaw(vec(8,7.23)) * 3 + 81);
        return .04 * fsinosc(freq); 
    });
    out = combn(fadein(out, .1), .2, 4);
    outlet(out);
}

void mod1_test() {
    S o = bilin(fsinosc(1.4, 0), .1, .95);
    S in = .1 * lfsaw(vec(60,61), vec(0,.25));
    S out = in.chain(4, [=](S x){ return onepole(x, o); });
    outlet(out);
}
void mod4_test() {
    S o = bilin(fsinosc(.4, 0), .1, .95);
    S amp = .6 * decay2(lfimp(1./1.2), .01, .2);
    S in = lfzig(vec(200,251), vec(0,.25));
    S fout = amp * in.chain(4, [=](S x){ return onepole(x, o); });
    S out = combn(fout, vec(.3,.15), 4);
    outlet(out);
}
void mod5_test() {
    S dv = .2;
    S delayTimes = vec(.3,.15);
    S delayMod = lin(fsinosc(.5, 0), dv, 1);
    S delayMax = (1 + dv) * delayTimes;
    S amp = .6 * decay2(lfimp(1/1.2, 0), .01, .2);
    S in = lfzig(vec(200,251), vec(0,.25));
    S fout = amp * in.chain(4, [=](S x){ return onepole(x, .9); });
    S out = combl(fout, delayTimes * delayMod, delayMax, 4);
    outlet(out);
}
#endif

void init_urand_test() {
    S detune = vec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand(16, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, 16, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(oscs) * 0.1;
    outlet(osc_sum);
}

#if 0

void reduce_test() {
    f64 neutral_third = std::pow(2., 7./24.); // neutral third interval (7 quarter tones).
    S freqs = geo(8, 130.81, neutral_third); // row vector of 8 neutral third intervals.
    S detune = vec(-0.13, 0.13).transpose(); // 2 channel column vector.
    S detuned_freqs = freqs + detune; // column vector + row vector results in a matrix due to broadcasting.
    S oscs = cb(fsinosc(detuned_freqs)); // 2x8 matrix of sine^3 oscillators.
    S osc_sum = sum(oscs) * 0.1; // sum 8 columns to produce a stereo column vector.
    outlet(osc_sum);
}

void no_reduce_test() {
    f64 neutral_third = std::pow(2., 7./24.); // neutral third interval (7 quarter tones).
    S freqs = geo(8, 130.81, neutral_third); // row vector of 8 neutral third intervals.
    S detune = vec(-0.13, 0.13).transpose(); // 2 channel column vector.
    S detuned_freqs = freqs + detune; // column vector + row vector results in a matrix due to broadcasting.
    S oscs = cb(fsinosc(detuned_freqs)); // 2x8 matrix of sine^3 oscillators.
    outlet(oscs * 0.1);
}

void take_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand({2,8}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,8}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(take(oscs, 4)) * 0.1;
    outlet(osc_sum);
}

void skip_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand({2,8}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,8}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(skip(oscs, 4)) * 0.1;
    outlet(osc_sum);
}

void stride_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand({2,8}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,8}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(stride(oscs, 2)) * 0.1;
    outlet(osc_sum);
}

void no_reverse_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand({2,8}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,8}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(oscs) * 0.1;
    outlet(osc_sum);
}

void reverse_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand({2,8}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,8}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(reverse(oscs, Col)) * 0.1;
    outlet(osc_sum);
}

void rotate_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand({2,8}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,8}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(rotate(oscs, 2, Col)) * 0.1;
    outlet(osc_sum);
}

void permute_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 4, initSignalRate) + detune;
    S amp_phases = urand({2,4}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,4}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S p = vec(1, 0, 3, 2, 3, 2, 1, 0);
    S osc_sum = sum(permute(oscs, p, Col)) * 0.1;
    outlet(osc_sum);
}


void reverse_rows_test() {
    S detune = colvec(-0.1, 0.1);
    S freqs = xrand(100, 600, 8, initSignalRate) + detune;
    S amp_phases = urand({2,8}, initSignalRate);
    S amp_freqs = xrand(0.1, 0.5, {2,8}, initSignalRate);
    S oscs = cb(fsinosc(freqs)) * uni(fsinosc(amp_freqs, amp_phases));
    S osc_sum = sum(reverse(oscs, Row)) * 0.1;
    outlet(osc_sum);
}

void for_test() {
    f64 neutral_third = std::pow(2., 7./24.); // neutral third interval (7 quarter tones).
    S freqs = geo(8, 130.81, neutral_third); // row vector of 8 neutral third intervals.
    S detune = vec(-0.13, 0.13).transpose(); // 2 channel column vector.
    S detuned_freqs = freqs + detune; // column vector + row vector results in a matrix due to broadcasting.
    
    S osc_sum = for_(2, [&](S _i){
        S oscs = cb(fsinosc(detuned_freqs)); // 2x8 matrix of sine^3 oscillators.
        return sum(oscs) * 0.1; // sum 8 columns to produce a stereo column vector.
    });
    outlet(osc_sum);
}
#endif


void tog_pause() {
    S s0 = lfusqr(1, 0);
    S s0f = lag(s0, .05) - .01;
    S s1f = lag(1 - s0, .05) - .01;
    S out1 = pause(s0f, [](){ return .1 * blue(2); }) + pause(s1f, [](){ return .15 * cb(max(-.2, fsinxosc(100 + white(2), 0))); });
    S out = combn(fadein(out1, .1), .2, 2);
    outlet(out);
}

void switch_test() {
    S d = vec(-0.1f, 0.1f);
    S s = cast_i32(bilin(fsinosc(0.15), 0, 7.99));
    // this is only for testing. switching among fsinoscs will produce clicks.
    S w = switch_(s, 
        [&](){ return fsinosc(240+d); },
        [&](){ return fsinosc(270+d); },
        [&](){ return fsinosc(300+d); },
        [&](){ return fsinosc(320+d); },
        [&](){ return fsinosc(360+d); },
        [&](){ return fsinosc(400+d); },
        [&](){ return fsinosc(450+d); },
        [&](){ return fsinosc(480+d); });
    outlet(lag(w * 0.2, 0.001));
}

void select_test() {
    S d = vec(-0.1f, 0.1f);
    S s = cast_i32(bilin(fsinosc(0.15), 0, 7.99));
    // this is only for testing. selecting among fsinoscs will produce clicks.
    S w = sel(s,
        fsinosc(240+d), fsinosc(270+d), fsinosc(300+d), fsinosc(320+d), 
        fsinosc(360+d), fsinosc(400+d), fsinosc(450+d), fsinosc(480+d) );
    outlet(lag(w * 0.2, 0.001));
}

void pch_seq() {
    S pch = lag(pull(lfimp(3, 0), [](){ return xrand(125, 1000); }, 200), .05);
    S out = .1 * cb(fsinxosc(pch + vec(0,1), 0));
    S fx = combn(fadein(out, .1), .2, 4);
    outlet(fx);
}

void pull_nested() {
    S gate1 = max(0, fsinosc(.3));
    S out = pause(gate1, [](){
        S gate2 = max(0, fsinosc(2));
        S out = pause(gate2, [](){
            S freq = nnhz(81 + 24 * lfsaw(.4) + 3 * lfsaw(vec(8, 7.32)));
            return .04 * fsinxosc(freq);
        });
        return combn(fadein(out, .1), .2, 4);
    });
    outlet(out);
}

void pulltwo() {
    auto pullFun1 = [](){
        S amp = .2 * decay2(dust(4, 2), .04, .3);
        return amp * fsinxosc(800);
    };
    auto pullFun2 = [](){
        S freq = nnhz(81 + 24 * lfsaw(.4) + 3 * lfsaw(vec(8, 7.32)));
        S out = .04 * fsinxosc(freq);
        return combn(fadein(out, .1), .2, 4);
    };
    S gate1 = max(0, fsinosc(2.71828));
    S gate2 = max(0, fsinosc(3.14159));
    S out = gate1 * pause(gate1, pullFun1) + gate2 * pause(gate2, pullFun2);
    outlet(out);
}

} // namespace synthdef
