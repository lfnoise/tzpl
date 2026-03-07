-- Example synthdefs

import synthdefs.*;
import common_ugens.*;

fn bubbles() S =
	0.4 lfsaw * 24
	+ [8, 7.23] lfsaw * 3
	+ 81
	|> nnhz sinosc * 0.04
	|> combn(0.2, 4) outlet;

bubbles defSynth("bubbles") println;

fn dustone() = 0.2 * decay2(dust(4, 2), 0.04, 0.3) * 800 fsinxosc |> outlet;

dustone defSynth("dustone") println;

fn init_urand_test() S {
	let detune = [-1, 1] vec;
	let freqs = exprand(100, 600, 8, Rate.init) + detune;
	let ampPhases = urand(16, Rate.init);
	let ampFreqs = exprand(0.1, 0.5, 16, Rate.init);
	let oscs = freqs fsinosc cb * ampFreqs fsinosc(ampPhases) uni;
	oscs sum(2) * 0.1 |> outlet
}

init_urand_test defSynth("init_urand_test") println;


fn pause_bubbles() S {
    let gate = 0.5 sinosc - 0.5;
    let out = gate pause(fn(){   
        let freq = nnhz(0.4 lfsaw * 24 + [8, 7.23] lfsaw * 3 + 81);
        0.05 * freq fsinosc 
	});
    out fadein(0.1) combn(0.2, 4) outlet
}

pause_bubbles defSynth("pause_bubbles") println;

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

pull_nested defSynth("pull_nested") println;


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

pulltwo defSynth("pulltwo") println;


fn pch_seq() S {
    let pch = 3 lfimp pull(200, fn(){ exprand(125, 1000) });
    let out = 0.1 * fsinxosc(pch lag(0.05) + [0,1]) cb;
    out fadein(0.1) combn(0.2, 4) outlet
}


pch_seq defSynth("pch_seq") println;


fn() S { 0.3 sinosc biexp(100, 6000) velvet(2) * 0.2 |> outlet } defSynth("dust1") println;


fn sahtone1() S {
	let freq = 2 white sampleAndHold(coin(3*T())) biexp(120, 800) lag(0.04);
	let in = freq lftri * 0.2;
	in combn(0.2, 2) outlet
}

sahtone1 defSynth("sahtone1") println;



fn sahtone2() S {
	let freq = 2 white sampleAndHold(3 lfimp) biexp(120, 800) lag(0.04);
	let in = freq fsinxosc tanh * 0.2;
	in combn(0.2, 2) outlet
}

sahtone2 defSynth("sahtone2") println;


fn mod1_test() S {
	let c = 1.4 fsinosc bilin(0.1, 0.95);
	let in = [60, 61] lfsaw([0, 0.25]) * 0.1;
	in chain(4, fn(x S)S{x onepole(c)}) outlet
}

mod1_test defSynth("mod1_test") println;

fn mod4_test() S {
	let c S = 0.4 fsinosc bilin(0.1, 0.95);
	let amp S = lfimp(1 / 1.2) decay2(0.01, 0.2) * 0.6;
	let in S = [200, 251] lfsaw([0, 0.25]);
	let out S = amp * in chain(4, fn(x S)S{x onepole(c)});
	out combn([0.3,0.15], 4) outlet
}

mod4_test defSynth("mod4_test") println;

fn mod5_test() S {
	let dv = 0.2;
	let delayTimes = [0.3, 0.15];
	let delayMod = 0.5 fsinosc lin(dv, 1);
	let delayMax = (1 + dv) * delayTimes;
	let amp = lfimp(1 / 1.2) decay2(0.01, 0.2) * 0.6;
	let in = [200, 251] lfzig([0, 0.25]);
	let out = amp * in chain(4, fn(x S){x onepole(0.9)});
	out combl(delayTimes * delayMod, delayMax, 4) outlet
}

mod5_test defSynth("mod5_test") println;


fn white_test() S = outlet(2 white * 0.2);

white_test defSynth("white_test") println;


fn pink_test() S = outlet(2 pinkf * 0.2);

pink_test defSynth("pink_test") println;


fn violet_test() S = outlet(2 violet * 0.2);

violet_test defSynth("violet_test") println;


fn blue_test() S = outlet(2 blue * 0.2);

blue_test defSynth("blue_test") println;


fn red_test() S = outlet(2 red * 0.2);

red_test defSynth("red_test") println;

fn bubbles_lite() S = 0.4 lfsaw * 24 + 8 lfsaw * 3 + 81 |> nnhz sinosc * 0.04 |> outlet;

-- bubbles_lite defSynth("bubbles_lite")

---------------------------------------------------------------------------
-- Play a demo

"init_urand_test" playFor(5.0);
/*
"bubbles" playFor(5.0);
"dustone" playFor(5.0);
"pause_bubbles" playFor(5.0);
"tog_pause" playFor(5.0);
"pull_nested" playFor(5.0);
"pulltwo" playFor(5.0);
"pch_seq" playFor(5.0);
"sahtone1" playFor(5.0);
"sahtone2" playFor(5.0);
"mod1_test" playFor(5.0);
"mod4_test" playFor(5.0);
"mod5_test" playFor(5.0);
"white_test" playFor(5.0);
"pink_test" playFor(5.0);
"violet_test" playFor(5.0);
"blue_test" playFor(5.0);
"red_test" playFor(5.0);
*/
