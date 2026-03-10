-- Test vector operations in synthdef context

import synthdef.*;
import common_ugens.*;

-- take: first 4 of 8 channels
fn test_take() S {
	let sig = [1, 2, 3, 4, 5, 6, 7, 8] vec;
	sig take(4) outlet
}

test_take defSynth("test_take") println;

-- drop: skip first 4 of 8 channels, leaving 4
fn test_drop() S {
	let sig = [1, 2, 3, 4, 5, 6, 7, 8] vec;
	sig drop(4) outlet
}

test_drop defSynth("test_drop") println;

-- reverse: reverse channel order
fn test_reverse() S {
	let sig = [1, 2, 3, 4] vec;
	sig reverse outlet
}

test_reverse defSynth("test_reverse") println;

-- stride: every 2nd channel
fn test_stride() S {
	let sig = [1, 2, 3, 4, 5, 6, 7, 8] vec;
	sig stride(2) outlet
}

test_stride defSynth("test_stride") println;

-- stutter: repeat each channel 2 times
fn test_stutter() S {
	let sig = [1, 2, 3, 4] vec;
	sig stutter(2) outlet
}

test_stutter defSynth("test_stutter") println;

-- ncyc: cycle the input 4 times
fn test_ncyc() S {
	let sig = [1, 2] vec;
	sig ncyc(4) outlet
}

test_ncyc defSynth("test_ncyc") println;

-- transpose: 2x4 -> 4x2
fn test_transpose() S {
	let sig = [1, 2, 3, 4, 5, 6, 7, 8] vec;
	sig transpose(4) outlet
}

test_transpose defSynth("test_transpose") println;

-- rotate: shift channels by a constant
fn test_rotate() S {
	let sig = [1, 2, 3, 4] vec;
	sig rotate(1) outlet
}

test_rotate defSynth("test_rotate") println;

-- join: concatenate two signals
fn test_join() S {
	let a = [1, 2] vec;
	let b = [3, 4] vec;
	join([a, b]) outlet
}

test_join defSynth("test_join") println;

-- at: index into a signal
fn test_at() S {
	let sig = [100, 200, 300, 400] vec;
	let indices = [3, 1, 0, 2] vec;
	sig at(indices) outlet
}

test_at defSynth("test_at") println;

-- sum: reduce by addition
fn test_sum() S {
	let sig = [1, 2, 3, 4] vec;
	sig sum outlet
}

test_sum defSynth("test_sum") println;

-- sum with output channels
fn test_sum_stereo() S {
	let sig = [1, 2, 3, 4, 5, 6, 7, 8] vec;
	sig sum(2) outlet
}

test_sum_stereo defSynth("test_sum_stereo") println;

-- reduce with min
fn test_minOf() S {
	let sig = [4, 1, 3, 2] vec;
	sig minOf outlet
}

test_minOf defSynth("test_minOf") println;

-- reduce with max
fn test_maxOf() S {
	let sig = [4, 1, 3, 2] vec;
	sig maxOf outlet
}

test_maxOf defSynth("test_maxOf") println;

-- product
fn test_product() S {
	let sig = [1, 2, 3, 4] vec;
	sig product outlet
}

test_product defSynth("test_product") println;

-- Composed: take + reverse
fn test_compose_take_reverse() S {
	let sig = [1, 2, 3, 4, 5, 6, 7, 8] vec;
	sig take(4) reverse outlet
}

test_compose_take_reverse defSynth("test_compose_take_reverse") println;

-- Composed: stutter + sum
fn test_stutter_sum() S {
	let sig = [100, 200, 300, 400] vec;
	sig stutter(2) sum(2) outlet
}

test_stutter_sum defSynth("test_stutter_sum") println;

-- Audio example: multichannel detuned oscillators summed to stereo
fn test_detune_sum() S {
	let freqs = [220, 221, 330, 331, 440, 441, 550, 551] vec;
	freqs fsinosc * 0.05 |> sum(2) outlet
}

test_detune_sum defSynth("test_detune_sum") println;

-- Audio example: stereo oscillators with reverse panning
fn test_reverse_pan() S {
	let sig = [440, 550] fsinosc * 0.1;
	let left = sig;
	let right = sig reverse;
	join([left, right]) outlet
}

test_reverse_pan defSynth("test_reverse_pan") println;


"test_detune_sum" playFor(4);
"test_reverse_pan" playFor(4);

