-- Fasta-style benchmark: the LCG + weighted-table lookup core of the Computer
-- Language Benchmarks Game `fasta` task. Instead of writing millions of
-- characters to stdout (which would make this an IO benchmark), we tally the
-- per-nucleotide counts and print those. The hot path is identical to the
-- canonical: one LCG step, one cumulative-probability linear scan, one
-- counter increment, per random draw.

const N_STEPS = 5000000;
const IM = 139968;
const FIM = 139968.0;

-- Cumulative probabilities for IUB nucleotides (15 entries).
const P0 = 0.27;
const P1 = 0.27 + 0.12;
const P2 = 0.27 + 0.12 + 0.12;
const P3 = 0.27 + 0.12 + 0.12 + 0.27;
const P4 = P3 + 0.02;
const P5 = P3 + 0.04;
const P6 = P3 + 0.06;
const P7 = P3 + 0.08;
const P8 = P3 + 0.10;
const P9 = P3 + 0.12;
const P10 = P3 + 0.14;
const P11 = P3 + 0.16;
const P12 = P3 + 0.18;
const P13 = P3 + 0.20;

var seed = 42;

-- Tallies for each of 15 IUB nucleotides (a c g t B D H K M N R S V W Y).
var c0 = 0; var c1 = 0; var c2 = 0; var c3 = 0; var c4 = 0;
var c5 = 0; var c6 = 0; var c7 = 0; var c8 = 0; var c9 = 0;
var c10 = 0; var c11 = 0; var c12 = 0; var c13 = 0; var c14 = 0;

var i = 0;
while (i < N_STEPS) {
    seed = (seed * 3877 + 29573) % IM;
    let p = toFloat(seed) / FIM;
    if (p < P0) { c0 = c0 + 1; }
    else if (p < P1) { c1 = c1 + 1; }
    else if (p < P2) { c2 = c2 + 1; }
    else if (p < P3) { c3 = c3 + 1; }
    else if (p < P4) { c4 = c4 + 1; }
    else if (p < P5) { c5 = c5 + 1; }
    else if (p < P6) { c6 = c6 + 1; }
    else if (p < P7) { c7 = c7 + 1; }
    else if (p < P8) { c8 = c8 + 1; }
    else if (p < P9) { c9 = c9 + 1; }
    else if (p < P10) { c10 = c10 + 1; }
    else if (p < P11) { c11 = c11 + 1; }
    else if (p < P12) { c12 = c12 + 1; }
    else if (p < P13) { c13 = c13 + 1; }
    else { c14 = c14 + 1; }
    i = i + 1;
}

c0 println; c1 println; c2 println; c3 println; c4 println;
c5 println; c6 println; c7 println; c8 println; c9 println;
c10 println; c11 println; c12 println; c13 println; c14 println;
