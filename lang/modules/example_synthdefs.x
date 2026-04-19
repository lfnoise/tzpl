-- Example synthdefs

import synthdef.*;
import common_ugens.*;

fn init_urand_test() S {
	let chans = 16;
	let detune = [-1, 1] vec;
	let freqs = exprand(100, 600, 8, Rate.init) + detune;
	let ampPhases = urand(chans, Rate.init);
	let ampFreqs = exprand(0.1, 0.5, chans, Rate.init);
	let amps = ampFreqs fsinosc(ampPhases) uni;
	let oscs = freqs fsinosc cb * amps;
	oscs sum(2) * 0.1 |> outlet
}

init_urand_test defSynth("init_urand_test");


fn bubbles() S =
	0.4 lfsaw * 24
	+ [8, 7.23] lfsaw * 3
	+ 81
	|> nnhz sinosc * 0.04
	|> combn(0.2, 4) outlet;

bubbles defSynth("bubbles");


fn blipTest() S {
	let h = 1/20 sinosc(0.75) bilin(1, 48);
	[36, 36.13] blip(0, h) mul(0.2) fadein(0.1) outlet
}

blipTest defSynth("blipTest");


fn smoothSawTest() S {
	let s = 1/5 sinosc(0.75) bilin(1, 8);
	[100, 100.13] smoothSaw(s) mul(0.2) fadein(0.1) outlet
}

smoothSawTest defSynth("smoothSawTest");


fn smoothSquareTest() S {
	let s = 1/5 sinosc(0.75) bilin(1, 8);
	[100, 100.13] smoothSquare(s) mul(0.2) fadein(0.1) outlet
}

smoothSquareTest defSynth("smoothSquareTest");


fn usinWinUsinTest() S {
	let s = 1/5 sinosc(0.75) bilin(1, 16);
	[100, 100.13] usinWinUsin(s) mul(0.2) fadein(0.1) outlet

}

usinWinUsinTest defSynth("usinWinUsinTest");

fn usinWinSinTest() S {
	let s = 1/5 sinosc(0.75) bilin(1, 16);
	[100, 100.13] usinWinSin(s) mul(0.2) fadein(0.1) outlet

}

usinWinSinTest defSynth("usinWinSinTest");


fn sawWinUsinTest() S {
	let s = 1/5 sinosc(0.75) bilin(1, 16);
	[100, 100.13] sawWinUsin(s) mul(0.2) fadein(0.1) outlet

}

sawWinUsinTest defSynth("sawWinUsinTest");


fn sawWinSinTest() S {
	let s = 1/5 sinosc(0.75) bilin(1, 16);
	[100, 100.13] sawWinSin(s) mul(0.2) fadein(0.1) outlet

}

sawWinSinTest defSynth("sawWinSinTest");

fn() S { 0.3 sinosc biexp(100, 6000) velvet(2) * 0.2 |> outlet } defSynth("dust1");

fn dustone() = 0.2 * decay2(dust(4, 2), 0.04, 0.3) * 800 fsinxosc |> outlet;

dustone defSynth("dustone");

fn apverbTest() S {
	let chans = 16;
	let freqs = exprand(100, 5000, chans, Rate.init);
	(3/chans) pandust(chans) decay2(0.004, 0.2) * freqs sinosc |> transpose(chans) sum(2) * 0.5 |> apverb(0.02, 6, 8) outlet
}

apverbTest defSynth("apverbTest");

fn pause_bubbles() S {
    let gate = 0.5 sinosc - 0.5;
    let out = gate pause(fn(){   
        let freq = nnhz(0.4 lfsaw * 24 + [8, 7.23] lfsaw * 3 + 81);
        0.05 * freq fsinosc 
	});
    out fadein(0.1) combn(0.2, 4) outlet
}

pause_bubbles defSynth("pause_bubbles");

fn tog_pause() S {
    let s0 = 1 lfusqr;
    let s0f = s0      lag(0.05) - 0.01;
    let s1f = s0 cmpl lag(0.05) - 0.01;
    let out = s0f pause(fn(){ 0.1 * 2 blue }) + s1f pause(fn(){ fsinxosc(100 + 2 white) max(-0.2) cb * 0.15  });
    out fadein(0.1) combn(0.2, 2) outlet
}

tog_pause defSynth("tog_pause")


fn pull_nested() S {
	let gate1 = 0.3 fsinosc max0;
    let out = gate1 pause(fn(){ 
        let gate2 = 2 fsinosc max0;
        let out = gate2 pause(fn(){
       		let freq = nnhz(81 + 24 * 0.4 lfsaw + 3 * [8, 7.23] lfsaw);
            0.04 * freq fsinxosc
		});
        out fadein(0.1) combn(0.2, 4)
	});
    out outlet
}

pull_nested defSynth("pull_nested");


fn pulltwo() S {
    let pullFun1 = fn(){
        let amp = 0.2 * dust(4, 2) decay2(0.04, 0.3);
        amp * 800 fsinxosc
    };
    let pullFun2 = fn(){
       	let freq = nnhz(81 + 24 * 0.4 lfsaw + 3 * [8, 7.23] lfsaw);
        0.04 * fsinxosc(freq) fadein(0.1) combn(0.2, 4)
    };
    let gate1 = 2.71828 fsinosc max0;
    let gate2 = 3.14159 fsinosc max0;
    let out = gate1 pause(pullFun1) + gate2 pause(pullFun2);
    out outlet
}

pulltwo defSynth("pulltwo");


fn pch_seq() S {
    let pch = 3 lfimp pull(200, fn(){ exprand(125, 1000) });
    let out = 0.1 * fsinxosc(pch lag(0.05) + [0,1]) cb;
    out fadein(0.1) combn(0.2, 4) outlet
}


pch_seq defSynth("pch_seq");



fn sahtone1() S {
	let freq = 2 white sampleAndHold(coin(3*T())) biexp(120, 800) lag(0.04);
	let in = freq lftri * 0.2;
	in combn(0.2, 2) outlet
}

sahtone1 defSynth("sahtone1");



fn sahtone2() S {
	let freq = 2 white sampleAndHold(3 lfimp) biexp(120, 800) lag(0.04);
	let in = freq fsinxosc tanh * 0.2;
	in combn(0.2, 2) outlet
}

sahtone2 defSynth("sahtone2");


fn mod1_test() S {
	let c = 1.4 fsinosc bilin(0.1, 0.95);
	let in = [60, 61] lfsaw([0, 0.25]) * 0.1;
	in chain(4, fn(x S)S{x onepole(c)}) outlet
}

mod1_test defSynth("mod1_test");

fn mod4_test() S {
	let c S = 0.4 fsinosc bilin(0.1, 0.95);
	let amp S = lfimp(1 / 1.2) decay2(0.01, 0.2) * 0.6;
	let in S = [200, 251] lfsaw([0, 0.25]);
	let out S = amp * in chain(4, fn(x S)S{x onepole(c)});
	out combn([0.3,0.15], 4) outlet
}

mod4_test defSynth("mod4_test");

fn mod5_test() S {
	let dv = 0.2;
	let delayTimes = [0.3, 0.15];
	let delayMod = 0.5 fsinosc lin(dv, 1);
	let delayMax = (1 + dv) * delayTimes;
	let amp = lfimp(1 / 1.2) decay2(0.01, 0.2) * 0.6;
	let in = [200, 251] lfzig([0, 0.25]);
	let out = amp * in chain(4, fn(x S){x onepole(0.9)});
	out comb(delayTimes * delayMod, delayMax, 4) outlet
}

mod5_test defSynth("mod5_test");


fn white_test() S = outlet(2 white * 0.2);

white_test defSynth("white_test");


fn pink_test() S = outlet(2 pinkf * 0.15);

pink_test defSynth("pink_test");


fn violet_test() S = outlet(2 violet * 0.2);

violet_test defSynth("violet_test");


fn blue_test() S = outlet(2 blue * 0.2);

blue_test defSynth("blue_test");


fn red_test() S = outlet(2 red * 0.2);

red_test defSynth("red_test");


fn gray_test() S = outlet(2 gray * 0.2);

gray_test defSynth("gray_test");


fn bubbles_lite() S = 0.4 lfsaw * 24 + 8 lfsaw * 3 + 81 |> nnhz sinosc * 0.04 |> outlet;

-- bubbles_lite defSynth("bubbles_lite")


---------------------------------------------------------------------------
-- Play a demo

import audio_engine as ae;
ae.listSynthDefs() println;

fn playExamples() {
    go(coro fn() Float {
    	"start playing" println;

    	"bubbles" playFor(5.0) yieldAll;
    	"smoothSquareTest" playFor(5.0) yieldAll;
    	"smoothSawTest" playFor(5.0) yieldAll;	
    	"blipTest" playFor(20.0) yieldAll;
    	"usinWinUsinTest" playFor(5.0) yieldAll;
    	"usinWinSinTest" playFor(5.0) yieldAll;
    	"sawWinUsinTest" playFor(5.0) yieldAll;
    	"sawWinSinTest" playFor(5.0) yieldAll;
    	"init_urand_test" playFor(5.0) yieldAll;
    	"dust1" playFor(5.0) yieldAll;
    	"dustone" playFor(5.0) yieldAll;
    	"apverbTest" playFor(10.0) yieldAll;
    	"pause_bubbles" playFor(5.0) yieldAll;
    	"tog_pause" playFor(5.0) yieldAll;
    	"pull_nested" playFor(5.0) yieldAll;
    	"pulltwo" playFor(5.0) yieldAll;
    	"pch_seq" playFor(5.0) yieldAll;
    	"sahtone1" playFor(5.0) yieldAll;
    	"sahtone2" playFor(5.0) yieldAll;
    	"mod1_test" playFor(5.0) yieldAll;
    	"mod4_test" playFor(5.0) yieldAll;
    	"mod5_test" playFor(5.0) yieldAll;
    	"violet_test" playFor(5.0) yieldAll;
    	"blue_test" playFor(5.0) yieldAll;
    	"white_test" playFor(5.0) yieldAll;
    	"pink_test" playFor(5.0) yieldAll;
    	"red_test" playFor(5.0) yieldAll;
    	"gray_test" playFor(5.0) yieldAll;

    	ae.endRender(0.1);
    
    	"stop playing" println;
    }());
}




fn renderExamples() {
	let h = "/tmp/examples.wav" ae.renderNRT(180, playExamples);
	ae.onRenderDone(h, fn(){ "render done" println });
}


-- do non-real-time rendering and real-time play back concurrently.
--renderExamples();
playExamples();















