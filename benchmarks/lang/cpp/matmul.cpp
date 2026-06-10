// Dense matrix multiply, same algorithm as the Lua/Tzopilotl ports.
#include <cstdio>
#include <cmath>
#include <vector>

static const int N = 500;
static const double FN = 500.0;

static std::vector<std::vector<double>> init_matrix(double phase) {
    std::vector<std::vector<double>> rows(N, std::vector<double>(N));
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            rows[i][j] = std::sin(i * FN + j + phase);
    return rows;
}

int main() {
    auto A = init_matrix(0.0);
    auto B = init_matrix(0.5);

    std::vector<std::vector<double>> C(N, std::vector<double>(N));
    for (int i = 0; i < N; ++i) {
        auto const& Ai = A[i];
        for (int j = 0; j < N; ++j) {
            double s = 0.0;
            for (int k = 0; k < N; ++k) s += Ai[k] * B[k][j];
            C[i][j] = s;
        }
    }

    double trace = 0.0;
    for (int i = 0; i < N; ++i) trace += C[i][i];
    std::printf("%.15g\n", trace);
}
