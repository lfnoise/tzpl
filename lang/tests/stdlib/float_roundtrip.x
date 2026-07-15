-- Bit-identical print/parse round-tripping of floats.
--
-- Printing uses shortest round-trip form (std::to_chars); float literals
-- and parseFloat both go through parseFloatC (C-locale, correctly rounded
-- strtod). So for every Float v: parseFloat(v toString) unwrap == v, and a
-- printed value pasted back as a literal denotes the same bits.
import std.test.*;

fn roundTrips(v Float, label String) Bool {
    assertEq(parseFloat(v toString) unwrap, v, label)
}

roundTrips(0.1, "0.1");
roundTrips(0.2, "0.2");
roundTrips(0.30000000000000004, "0.1+0.2 sum");
roundTrips(1.0 / 3.0, "one third");
roundTrips(5e-324, "min subnormal");
roundTrips(4.9406564584124654e-324, "min subnormal long form");
roundTrips(2.2250738585072014e-308, "min normal");
roundTrips(2.2250738585072011e-308, "halfway hard case");
roundTrips(1.7976931348623157e308, "max double");
roundTrips(9007199254740993.0, "2^53+1 rounds");
roundTrips(1e22, "1e22");
roundTrips(1e23, "1e23 near-halfway");
roundTrips(6.62607015e-34, "planck");
roundTrips(8.98846567431158e307, "2^1023");
roundTrips(-7.2057594037927933e16, "negative large");
roundTrips(123456.78901234567, "many digits");

-- The printed shortest forms themselves stay stable.
0.1 println;
5e-324 println;
1.7976931348623157e308 println;
9007199254740993.0 println;
(0.1 + 0.2) println;

let failures = testSummary();
failures println;
