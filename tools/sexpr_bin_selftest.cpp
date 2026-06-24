// Standalone self-test for shared/tzpl_sexpr_bin.hpp.
//
// Verifies the C++ encoder/Reader round-trips, and prints the encoded bytes as
// a decimal sequence so it can be diffed against lang/modules/message.x's
// encoding of the same value (proving the two implementations agree on the
// wire format). See integration-tests/scripts/sexpr_bin_interop.sh.
//
// Build:  c++ -std=c++23 -I shared tools/sexpr_bin_selftest.cpp -o /tmp/sbin_selftest

#include "tzpl_sexpr_bin.hpp"
#include <cstdio>

using namespace tzpl::sbin;

int main() {
    Value v = Value::Vec({
        Value::Symbol("note"),
        Value::Int(60),
        Value::Float(0.5),
        Value::String("hi"),
        Value::Vec({ Value::Int(1), Value::Bool(true) }),
    });

    std::vector<std::uint8_t> buf = encode(v);

    // Decimal byte dump (compared against the lang encoder).
    for (std::uint8_t byte : buf) std::printf("%d ", byte);
    std::printf("\n");

    // Round-trip via the zero-copy Reader.
    if (!Reader::valid(buf.data(), buf.size())) { std::printf("INVALID\n"); return 1; }
    Reader r = Reader::root(buf.data(), buf.size());
    std::printf("count=%u\n", r.childCount());
    std::printf("c0=%.*s\n", (int)r.child(0).asStr().size(), r.child(0).asStr().data());
    std::printf("c1=%lld\n", (long long)r.child(1).asInt());
    std::printf("c2=%g\n", r.child(2).asFloat());
    std::printf("c3=%.*s\n", (int)r.child(3).asStr().size(), r.child(3).asStr().data());
    Reader nested = r.child(4);
    std::printf("c4.count=%u c4.0=%lld c4.1=%d\n",
                nested.childCount(),
                (long long)nested.child(0).asInt(),
                nested.child(1).asBool());
    return 0;
}
