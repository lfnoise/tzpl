// Binary trees. Allocation stress test from the Computer Language Benchmarks
// Game, mirroring the Lua/Tzopilotl ports. Nodes are heap-allocated with `new`
// and freed per tree, the idiomatic manual-memory analogue of the GC versions.
#include <cstdio>

static const int MIN_DEPTH = 4;
static const int MAX_DEPTH = 16;

struct Tree {
    Tree* left;
    Tree* right;
};

static Tree* build(int depth) {
    if (depth <= 0) return new Tree{nullptr, nullptr};
    return new Tree{build(depth - 1), build(depth - 1)};
}

static int check(Tree const* t) {
    if (t->left) return 1 + check(t->left) + check(t->right);
    return 1;
}

static void destroy(Tree* t) {
    if (t->left) { destroy(t->left); destroy(t->right); }
    delete t;
}

int main() {
    int stretch_depth = MAX_DEPTH + 1;
    {
        Tree* t = build(stretch_depth);
        std::printf("stretch tree of depth %d\t check: %d\n", stretch_depth, check(t));
        destroy(t);
    }

    Tree* long_lived = build(MAX_DEPTH);

    for (int depth = MIN_DEPTH; depth <= MAX_DEPTH; depth += 2) {
        long long iterations = 1;
        for (int i = 0; i < MAX_DEPTH - depth + MIN_DEPTH; ++i) iterations *= 2;
        long long sum = 0;
        for (long long i = 0; i < iterations; ++i) {
            Tree* t = build(depth);
            sum += check(t);
            destroy(t);
        }
        std::printf("%lld\t trees of depth %d\t check: %lld\n", iterations, depth, sum);
    }

    std::printf("long lived tree of depth %d\t check: %d\n", MAX_DEPTH, check(long_lived));
    destroy(long_lived);
}
