// Same LCG + cumulative-table lookup as the Lua/Tzopilotl ports.
#include <cstdio>

static const int N_STEPS = 5000000;
static const long long IM = 139968;
static const double FIM = 139968.0;

int main() {
    const double P0  = 0.27;
    const double P1  = P0 + 0.12;
    const double P2  = P1 + 0.12;
    const double P3  = P2 + 0.27;
    const double P4  = P3 + 0.02;
    const double P5  = P3 + 0.04;
    const double P6  = P3 + 0.06;
    const double P7  = P3 + 0.08;
    const double P8  = P3 + 0.10;
    const double P9  = P3 + 0.12;
    const double P10 = P3 + 0.14;
    const double P11 = P3 + 0.16;
    const double P12 = P3 + 0.18;
    const double P13 = P3 + 0.20;

    long long seed = 42;
    long long c0=0,c1=0,c2=0,c3=0,c4=0;
    long long c5=0,c6=0,c7=0,c8=0,c9=0;
    long long c10=0,c11=0,c12=0,c13=0,c14=0;

    for (int i = 0; i < N_STEPS; ++i) {
        seed = (seed * 3877 + 29573) % IM;
        double p = seed / FIM;
        if      (p < P0 ) ++c0;
        else if (p < P1 ) ++c1;
        else if (p < P2 ) ++c2;
        else if (p < P3 ) ++c3;
        else if (p < P4 ) ++c4;
        else if (p < P5 ) ++c5;
        else if (p < P6 ) ++c6;
        else if (p < P7 ) ++c7;
        else if (p < P8 ) ++c8;
        else if (p < P9 ) ++c9;
        else if (p < P10) ++c10;
        else if (p < P11) ++c11;
        else if (p < P12) ++c12;
        else if (p < P13) ++c13;
        else              ++c14;
    }

    std::printf("%lld\n%lld\n%lld\n%lld\n%lld\n", c0, c1, c2, c3, c4);
    std::printf("%lld\n%lld\n%lld\n%lld\n%lld\n", c5, c6, c7, c8, c9);
    std::printf("%lld\n%lld\n%lld\n%lld\n%lld\n", c10, c11, c12, c13, c14);
}
