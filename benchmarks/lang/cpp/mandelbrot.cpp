// Mandelbrot set: count points inside the set on a WxH grid.
// Same algorithm as the Lua/Tzopilotl versions, using scalar doubles.
#include <cstdio>

static const int W = 1000, H = 1000, MAX_ITER = 100;

int main() {
    long long inside = 0;
    for (int py = 0; py < H; ++py) {
        double ci = 2.0 * py / H - 1.0;
        for (int px = 0; px < W; ++px) {
            double cr = 2.0 * px / W - 1.5;
            double zr = 0.0, zi = 0.0;
            bool escaped = false;
            for (int n = 0; n < MAX_ITER; ++n) {
                double zr2 = zr * zr;
                double zi2 = zi * zi;
                if (zr2 + zi2 > 4.0) { escaped = true; break; }
                zi = 2.0 * zr * zi + ci;
                zr = zr2 - zi2 + cr;
            }
            if (!escaped) ++inside;
        }
    }
    std::printf("%lld\n", inside);
}
