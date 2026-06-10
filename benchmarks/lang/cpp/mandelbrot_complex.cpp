// Mandelbrot using std::complex<double>, mirroring the Tzopilotl Complex port.
// Counts the same inside-set points as mandelbrot.cpp.
#include <cstdio>
#include <complex>

static const int W = 1000, H = 1000, MAX_ITER = 100;
static const double FW = 1000.0, FH = 1000.0;

static bool pixel_inside(std::complex<double> c) {
    std::complex<double> z(0.0, 0.0);
    for (int n = 0; n < MAX_ITER; ++n) {
        double zr = z.real();
        double zi = z.imag();
        if (zr * zr + zi * zi > 4.0) return false;
        z = z * z + c;
    }
    return true;
}

int main() {
    long long inside = 0;
    for (int py = 0; py < H; ++py) {
        double ci = 2.0 * py / FH - 1.0;
        for (int px = 0; px < W; ++px) {
            double cr = 2.0 * px / FW - 1.5;
            if (pixel_inside(std::complex<double>(cr, ci))) ++inside;
        }
    }
    std::printf("%lld\n", inside);
}
