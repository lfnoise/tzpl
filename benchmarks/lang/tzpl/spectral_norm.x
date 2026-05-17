-- Spectral norm. Iterative power method on an implicit matrix.
-- Tzopilotl arrays are immutable, so each pass rebuilds the vector with `map`.
-- Float loop counters avoid a const-folder bug with toFloat(int_loop_var).

const N = 1000;
const FN = 1000.0;

fn aij(i Float, j Float) Float = 1.0 / ((i + j) * (i + j + 1.0) * 0.5 + i + 1.0);

fn mul_av(v [Float], n Int) [Float] {
    let idx = (0..n-1) toArray;
    idx enumerate map(fn(p (Int, Int)) Float {
        let fi = toFloat(p.0);
        var s = 0.0;
        var fj = 0.0;
        var j = 0;
        while (j < n) {
            s = s + aij(fi, fj) * v[j];
            fj = fj + 1.0;
            j = j + 1;
        }
        s
    })
}

fn mul_atv(v [Float], n Int) [Float] {
    let idx = (0..n-1) toArray;
    idx enumerate map(fn(p (Int, Int)) Float {
        let fi = toFloat(p.0);
        var s = 0.0;
        var fj = 0.0;
        var j = 0;
        while (j < n) {
            s = s + aij(fj, fi) * v[j];
            fj = fj + 1.0;
            j = j + 1;
        }
        s
    })
}

fn mul_at_av(v [Float], n Int) [Float] = mul_atv(mul_av(v, n), n);

var u = repeat(1.0, N);
var v = repeat(0.0, N);
var iter = 0;
while (iter < 10) {
    v = mul_at_av(u, N);
    u = mul_at_av(v, N);
    iter = iter + 1;
}

var vBv = 0.0;
var vv = 0.0;
var i = 0;
while (i < N) {
    vBv = vBv + u[i] * v[i];
    vv = vv + v[i] * v[i];
    i = i + 1;
}

sqrt(vBv / vv) println;
