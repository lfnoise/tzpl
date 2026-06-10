// Fannkuch-redux. 0-indexed port of Mike Pall's algorithm, matching the
// Lua/Tzopilotl versions line-for-line so the comparison is apples-to-apples.
#include <cstdio>

static const int N = 10;

static void fannkuch(int n, long long& out_sum, long long& out_maxflips) {
    int p[64], q[64], s[64];
    for (int i = 0; i < n; ++i) { p[i] = i; q[i] = i; s[i] = i; }

    int sign = 1;
    long long maxflips = 0;
    long long sum = 0;

    while (true) {
        for (int j = 0; j < n; ++j) q[j] = p[j];
        long long flips = 0;
        int q0 = q[0];
        while (q0 != 0) {
            int lo = 0, hi = q0;
            while (lo < hi) {
                int tmp = q[lo]; q[lo] = q[hi]; q[hi] = tmp;
                ++lo; --hi;
            }
            ++flips;
            q0 = q[0];
        }
        if (flips > maxflips) maxflips = flips;
        sum += sign * flips;

        if (sign > 0) {
            int t = p[0]; p[0] = p[1]; p[1] = t;
            sign = -1;
        } else {
            int t = p[1]; p[1] = p[2]; p[2] = t;
            sign = 1;
            int i = 2;
            bool stop = false;
            while (i < n) {
                int si = s[i];
                if (si != 0) { s[i] = si - 1; break; }
                if (i == n - 1) { stop = true; break; }
                s[i] = i;
                int t2 = p[0];
                for (int k = 0; k <= i; ++k) p[k] = p[k + 1];
                p[i + 1] = t2;
                ++i;
            }
            if (stop) break;
        }
    }
    out_sum = sum;
    out_maxflips = maxflips;
}

int main() {
    long long sum, maxflips;
    fannkuch(N, sum, maxflips);
    std::printf("%lld\n", sum);
    std::printf("Pfannkuchen(%d) = %lld\n", N, maxflips);
}
