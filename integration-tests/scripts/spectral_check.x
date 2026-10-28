-- Regression: spectralChain. Compiles a set of spectral graphs with fixed
-- parameters, renders 2 s of each to $SPECTRAL_DIR/<name>.wav, and leaves
-- the numeric checks to spectral_check.py. Until Oct 2026 the body's
-- result never reached the inverse FFT (it overwrote the chain's output
-- sample), the body read the previous hop's frame, and two chains of one
-- fftSize shared a frame buffer -- every spectral effect was a no-op.
--
-- Two-channel renders are [dry, wet] unless noted, so the checker can
-- measure latency and gain against the input.
import synthdef.*;
import common_ugens.*;
import synthc.compile.*;
import std.result.*;
import audio_engine.*;
import filters.*;

let outDir = getEnv("SPECTRAL_DIR") unwrap;
let fails = &0;

fn src() S = (220.0 lfsaw * 0.2) + (white() * 0.1);
fn dryWet(x S, wet S) S = [x, wet] join outlet;
fn ident(x S, n Int, h Int) S = spectralChain(x, n, h, fn(frame S) { frame });

-- identity at several sizes and overlaps: unity gain, latency fftSize
fn gId256() S { let x = src(); dryWet(x, x ident(256, 128)) }
fn gId1024() S { let x = src(); dryWet(x, x ident(1024, 256)) }
fn gId2048() S { let x = src(); dryWet(x, x ident(2048, 256)) }
-- a body that scales: gain 0.5
fn gHalf() S { let x = src(); dryWet(x, spectralChain(x, 512, 128, fn(frame S) { frame * 0.5 })) }
-- two chains of one size in series: each owns its frame (gain 0.25, latency 2 fftSize)
fn gSeries() S {
	let x = src();
	let a = spectralChain(x, 512, 128, fn(frame S) { frame * 0.5 });
	dryWet(x, spectralChain(a, 512, 128, fn(frame S) { frame * 0.5 }))
}
-- a control read in the body
fn gControl() S {
	let x = src();
	let g = control("gain", lspec(0.0, 1.0, 0.25));
	dryWet(x, spectralChain(x, 512, 128, fn(frame S) { frame * g }))
}
-- stereo: the re/im split and rejoin is exact per channel, and a [2, 1, 1]
-- gain scales the channels separately (the right one by 0.5)
fn stereoSrc() S = [220.0, 331.0] lfsaw * 0.2;
fn stereoBody(x S) S = spectralChain(x, 512, 128, fn(frame S) {
	specComplex(frame specRe, frame specIm) * ([1.0, 0.5] asSignal reshape([2, 1, 1]))
});
fn gStereoL() S { let x = stereoSrc(); dryWet(x at(0), x stereoBody at(0)) }
fn gStereoR() S { let x = stereoSrc(); dryWet(x at(1), x stereoBody at(1)) }
-- brick-wall band-pass 1000..2000 Hz on white noise (wet only)
fn gBand() S {
	let x = white() * 0.3;
	spectralChain(x, 1024, 256, fn(frame S) {
		let f = specBinFreqs(1024);
		frame * ((f >= 1000.0) * (f <= 2000.0))
	}) outlet
}
-- shift a bin-centered sine (bin 22 of 1024 at 48k: 1031.25 Hz) up 4 bins
fn gShift() S {
	let x = 1031.25 sinosc * 0.3;
	let k = specBins(1024);
	spectralChain(x, 1024, 256, fn(frame S) {
		frame rotate(-4) * (k >= 4)
	}) outlet
}
-- per-bin gate at -40 dB on a sine in hiss: [ungated, gated]
fn gGate() S {
	let x = (1031.25 sinosc * 0.3) + (white() * 0.03);
	dryWet(x ident(1024, 256), spectralChain(x, 1024, 256, fn(frame S) {
		frame * (frame specAmps(1024) > (-40.0 dbamp))
	}))
}
-- fixed phases (zero, and pi k centering the pulse): the output repeats
-- every hop; [zero phase, centered]
fn gRobot() S {
	let x = src();
	let k = specBins(1024);
	[spectralChain(x, 1024, 256, fn(frame S) { specPolar(frame specMag, 0.0) }),
	 spectralChain(x, 1024, 256, fn(frame S) { specPolar(frame specMag, k * pi) })] join outlet
}
-- frame-to-frame state: magnitudes smoothed across frames converge to the input's
fn gSmooth() S {
	let x = 220.0 lfsaw * 0.2;
	dryWet(x, spectralChain(x, 512, 128, fn(frame S) {
		let m = frame specMag;
		let d = delayVar();
		let prev = d read(1);
		let sm = prev + ((m - prev) * 0.1);
		sm -> d;
		specPolar(sm, frame specPhase)
	}))
}
-- freeze: the input stops at 1 s; frozen from 0.5 s, so the wet side
-- keeps sounding; [unfrozen, frozen]
fn freezeBody(x S, freeze S) S = spectralChain(x, 1024, 256, fn(frame S) {
	let frozen = freeze > 0.5;
	let d = delayVar();
	let held = select2(frozen, d read(1), frame specMag);
	held -> d;
	select2(frozen, specPolar(held, urand(512) * twopi), frame)
});
fn gFreeze() S {
	let t = 0.5 phasor * 2.0;   -- seconds, 0 .. 2
	let x = (440.0 sinosc * 0.3) * (t < 1.0);
	dryWet(x freezeBody(0.0 asSignal), x freezeBody(t > 0.5))
}

-- a body that returns the wrong shape is a compile error
fn gBadShape() S = spectralChain(src(), 512, 128, fn(frame S) { frame specRe }) outlet;
match (gBadShape defSynthChecked("spc_bad") await) {
	ok(c): { fails <- *fails + 1; println("FAIL bad body shape compiled"); }
	err(m): {
		if (m contains("spectralChain: the body returned")) { println("PASS bad body shape rejected"); }
		else { fails <- *fails + 1; println("FAIL bad body shape: unexpected error: " $ m); }
	}
}

let cases = [
	("id256", gId256), ("id1024", gId1024), ("id2048", gId2048), ("half", gHalf),
	("series", gSeries), ("control", gControl), ("stereoL", gStereoL), ("stereoR", gStereoR),
	("band", gBand), ("shift", gShift), ("gate", gGate), ("robot", gRobot),
	("smooth", gSmooth), ("freeze", gFreeze),
];
for (c : cases) {
	let (name, f) = c;
	match (f defSynthChecked("spc_" $ name) await) {
		ok(cpp): {}
		err(m): { fails <- *fails + 1; println("FAIL compile " $ name $ ": " $ m); }
	}
}
if (*fails == 0) {
	for (c : cases) {
		let name = c.0;
		let h = renderNRT(outDir $ "/" $ name $ ".wav", 2.0, fn() Void {
			safetyLimiter(false);
			play("spc_" $ name);
		});
		await renderDone(h);
	}
	println("SPECTRAL RENDER ALL PASS");
} else {
	println("SPECTRAL COMPILE FAILED: " $ *fails toString);
}
