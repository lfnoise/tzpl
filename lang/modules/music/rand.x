-- rand.x -- random helpers the builtins do not cover.
--
-- The builtins already provide urand/brand/rand/irand/xrand/pick (scalars),
-- their infinite stream forms (urands/.../picks), the muss shuffle, and
-- randSeed(seed) for reproducible streams. This module adds the remaining
-- pieces the pattern libraries need.
--
-- RT-safe: pure math over the per-VM RNG, no IO.

-- True with probability p.
fn coin(p Float) Bool = urand() < p;

-- Normally distributed random value (Box-Muller).
fn gauss(mu Float, sigma Float) Float {
    let u1 = 1.0 - urand();          -- (0, 1]: keeps log finite
    let u2 = urand();
    mu + sigma * (0.0 - 2.0 * u1 log) sqrt * (6.283185307179586 * u2) cos
}

-- Weighted random choice: xs[i] with probability ws[i]/sum(ws).
-- (Stream form `wpicks` lives in music.pat, following pick/picks.)
fn wpick<T>(xs [T], ws [Float]) T {
    var r = urand() * ws sum;
    var i = 0;
    while (i + 1 < xs length) {
        r = r - ws[i];
        if (r < 0.0) { return xs[i]; }
        i = i + 1;
    }
    xs[i]
}
