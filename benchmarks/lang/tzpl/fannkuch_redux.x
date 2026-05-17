-- Fannkuch-redux. For each of the n! permutations of [0..n-1] (in Mike Pall's
-- alternating-sign order), reverse the first p[0]+1 elements until p[0]==0,
-- track the maximum flip count, and accumulate the signed checksum.
--
-- Tzopilotl arrays are immutable, so the permutation buffer, the working
-- copy, and the counter array are held in [Ref<Int>] with setref/deref for
-- in-place mutation. The extra indirection on every element access makes
-- this benchmark a worst case for Tzopilotl's value model.

const N = 9;

fn fannkuch(n Int) (Int, Int) {
    let p = (0..n-1) toArray map(fn(i Int) { ref(i) });
    let q = (0..n-1) toArray map(fn(i Int) { ref(i) });
    let s = (0..n-1) toArray map(fn(i Int) { ref(i) });

    var sign = 1;
    var maxflips = 0;
    var sum = 0;
    var keep_going = true;

    while (keep_going) {
        -- Copy p -> q.
        var j = 0;
        while (j < n) {
            setref(deref(p[j]), q[j]);
            j = j + 1;
        }

        -- Count flips on q until q[0] == 0.
        var flips = 0;
        var q0 = deref(q[0]);
        while (q0 != 0) {
            var lo = 0;
            var hi = q0;
            while (lo < hi) {
                let tmp = deref(q[lo]);
                setref(deref(q[hi]), q[lo]);
                setref(tmp, q[hi]);
                lo = lo + 1;
                hi = hi - 1;
            }
            flips = flips + 1;
            q0 = deref(q[0]);
        }
        if (flips > maxflips) { maxflips = flips; }
        sum = sum + sign * flips;

        -- Generate next permutation. Mike Pall: alternate single swap (sign=+1)
        -- with two swaps + counter walk (sign=-1).
        if (sign > 0) {
            let t = deref(p[0]);
            setref(deref(p[1]), p[0]);
            setref(t, p[1]);
            sign = -1;
        } else {
            let t = deref(p[1]);
            setref(deref(p[2]), p[1]);
            setref(t, p[2]);
            sign = 1;
            var i = 2;
            var done = false;
            while (i < n && !done) {
                let si = deref(s[i]);
                if (si != 0) {
                    setref(si - 1, s[i]);
                    done = true;
                } else {
                    if (i == n - 1) {
                        keep_going = false;
                        done = true;
                    } else {
                        setref(i, s[i]);
                        -- Rotate p[0..i+1] (inclusive on both ends, length i+2)
                        -- left by 1: the canonical 1-indexed version rotates
                        -- p[1..i+1] which is length i+1; translated to our
                        -- 0-indexed counter `i` (one less than canonical's i)
                        -- this becomes length i+2.
                        let t2 = deref(p[0]);
                        var k = 0;
                        while (k <= i) {
                            setref(deref(p[k + 1]), p[k]);
                            k = k + 1;
                        }
                        setref(t2, p[i + 1]);
                        i = i + 1;
                    }
                }
            }
        }
    }
    (sum, maxflips)
}

let result = fannkuch(N);
result.0 println;
("Pfannkuchen(" $ toString(N) $ ") = " $ toString(result.1)) println;
