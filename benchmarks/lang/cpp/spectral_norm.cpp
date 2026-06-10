// Spectral norm. Same algorithm as the Lua/Tzopilotl versions.
#include <cstdio>
#include <cmath>
#include <vector>

static const int N = 1000;

static double aij(int i, int j) {
    return 1.0 / ((i + j) * (i + j + 1.0) * 0.5 + i + 1.0);
}

static std::vector<double> mul_av(std::vector<double> const& v, int n) {
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < n; ++j) s += aij(i, j) * v[j];
        out[i] = s;
    }
    return out;
}

static std::vector<double> mul_atv(std::vector<double> const& v, int n) {
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) {
        double s = 0.0;
        for (int j = 0; j < n; ++j) s += aij(j, i) * v[j];
        out[i] = s;
    }
    return out;
}

static std::vector<double> mul_at_av(std::vector<double> const& v, int n) {
    return mul_atv(mul_av(v, n), n);
}

int main() {
    std::vector<double> u(N, 1.0);
    std::vector<double> v(N, 0.0);
    for (int k = 0; k < 10; ++k) {
        v = mul_at_av(u, N);
        u = mul_at_av(v, N);
    }

    double vBv = 0.0, vv = 0.0;
    for (int i = 0; i < N; ++i) {
        vBv += u[i] * v[i];
        vv  += v[i] * v[i];
    }

    std::printf("%.15g\n", std::sqrt(vBv / vv));
}
