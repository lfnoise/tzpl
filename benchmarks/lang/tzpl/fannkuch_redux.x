-- Fannkuch-redux. For each of the n! permutations of [0..n-1] (in Mike Pall's
-- alternating-sign order), reverse the first p[0]+1 elements until p[0]==0,
-- track the maximum flip count, and accumulate the signed checksum.
--
-- Uses mutable arrays: the permutation buffer, the working copy, and the
-- counter array are plain `[Int]` written through `p[i] = x`. This mirrors
-- the Lua reference implementation almost line for line.

const N = 10;

fn fannkuch(n Int) (Int, Int) {
    var p = (0..n-1) toArray;
    var q = (0..n-1) toArray;
    var s = (0..n-1) toArray;

    var sign = 1;
    var maxflips = 0;
    var sum = 0;
    var keep_going = true;

    while (keep_going) {
        -- Copy p -> q.
        var j = 0;
        while (j < n) {
            q[j] = p[j];
            j = j + 1;
        }

        -- Count flips on q until q[0] == 0.
        var flips = 0;
        var q0 = q[0];
        while (q0 != 0) {
            var lo = 0;
            var hi = q0;
            while (lo < hi) {
                let tmp = q[lo];
                q[lo] = q[hi];
                q[hi] = tmp;
                lo = lo + 1;
                hi = hi - 1;
            }
            flips = flips + 1;
            q0 = q[0];
        }
        if (flips > maxflips) { maxflips = flips; }
        sum = sum + sign * flips;

        -- Generate next permutation. Mike Pall: alternate single swap (sign=+1)
        -- with two swaps + counter walk (sign=-1).
        if (sign > 0) {
            let t = p[0];
            p[0] = p[1];
            p[1] = t;
            sign = -1;
        } else {
            let t = p[1];
            p[1] = p[2];
            p[2] = t;
            sign = 1;
            var i = 2;
            var done = false;
            while (i < n && !done) {
                let si = s[i];
                if (si != 0) {
                    s[i] = si - 1;
                    done = true;
                } else {
                    if (i == n - 1) {
                        keep_going = false;
                        done = true;
                    } else {
                        s[i] = i;
                        -- Rotate p[0..i+1] (inclusive on both ends, length i+2)
                        -- left by 1: the canonical 1-indexed version rotates
                        -- p[1..i+1] which is length i+1; translated to our
                        -- 0-indexed counter `i` (one less than canonical's i)
                        -- this becomes length i+2.
                        let t2 = p[0];
                        var k = 0;
                        while (k <= i) {
                            p[k] = p[k + 1];
                            k = k + 1;
                        }
                        p[i + 1] = t2;
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
