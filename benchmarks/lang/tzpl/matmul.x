-- Dense matrix multiply C = A * B for NxN matrices. Triple-nested loop with
-- a Float accumulator. Tests array indexing + dense FP arithmetic.
-- Output: trace(C) (sum of diagonal) for checksum.

const N = 200;
const FN = 200.0;

-- A[i][j] = sin(i*FN + j) and B[i][j] = cos(i*FN + j) -- chosen so the
-- matrix entries are bounded and the result is reproducible. Each row is
-- built in one shot via `map` over an index range; building rows by repeated
-- `$` concat would allocate O(N^2) intermediate arrays per row.
fn init_matrix(phase Float) [[Float]] {
    let idxs = (0..N-1) toArray;
    idxs map(fn(i Int) [Float] {
        let fi = toFloat(i);
        idxs map(fn(j Int) Float {
            sin(fi * FN + toFloat(j) + phase)
        })
    })
}

let A = init_matrix(0.0);
let B = init_matrix(0.5);

-- C[i][j] = sum_k A[i][k] * B[k][j]
let idxs = (0..N-1) toArray;
let C = idxs map(fn(i Int) [Float] {
    idxs map(fn(j Int) Float {
        var s = 0.0;
        var k = 0;
        while (k < N) {
            s = s + A[i][k] * B[k][j];
            k = k + 1;
        }
        s
    })
});

var trace = 0.0;
var i = 0;
while (i < N) {
    trace = trace + C[i][i];
    i = i + 1;
}
trace println;
