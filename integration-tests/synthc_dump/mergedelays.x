-- Graphs from the retired synthc_mergedelays_diff.x (synthc vs C++ oracle), kept as an
-- analysis-dump corpus for the block-engine compiler.

import synthdef.*;
import synthc.compile.*;
import common_ugens.*;

fn fb2() S {
	let drive = (fs() / 330.0) sin * 0.001;
	let y = delayVar(); y init(1, 0.0);
	let o = y read(1) * 0.7 + y read(2) * 0.2 + drive;
	y write(o); o outlet
}
fn diffsig() S {
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.0);
	a write((0.5 lfsaw) * 0.3);
	b write((0.5 lfsaw) * 0.4);   -- different gain -> different node
	(a read(1) + b read(1)) outlet
}
fn diffinit() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.5);
	a write(sig); b write(sig);
	(a read(1) + b read(1)) outlet
}
fn fbtwo() S {
	let x = (fs() / 200.0) sin * 0.001;
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.0);
	a write(a read(1) * 0.5 + x);
	b write(b read(1) * 0.9 + x);
	(a read(1) + b read(1)) outlet
}
fn twodel() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(1, 0.0);
	let b = delayVar(); b init(1, 0.0);
	a write(sig); b write(sig);
	(a read(1) + b read(1)) outlet
}
fn mtap() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(4, 0.0);
	let b = delayVar(); b init(4, 0.0);
	a write(sig); b write(sig);
	(a read(1) + b read(4)) outlet
}
fn three() S {
	let sig = (0.5 lfsaw) * 0.3;
	let a = delayVar(); a init(2, 0.0);
	let b = delayVar(); b init(2, 0.0);
	let c = delayVar(); c init(2, 0.0);
	a write(sig); b write(sig); c write(sig);
	(a read(1) + b read(2) + c read(1)) outlet
}

dumpCorpus([
	("fb2", fb2),
	("diffsig", diffsig),
	("diffinit", diffinit),
	("fbtwo", fbtwo),
	("twodel", twodel),
	("mtap", mtap),
	("three", three),
]);
