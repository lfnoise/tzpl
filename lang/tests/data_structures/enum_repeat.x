-- repeat on a payload-free enum value (an Int-backed array element) used to
-- leave a null array and crash on first use.
enum Level { low, mid, high }
var xs [Level] = Level.mid repeat(4);
println(xs length);
xs[2] = Level.high;
println(xs[0] ordinal, xs[2] ordinal, xs[3] ordinal);
var none [Level] = Level.low repeat(0);
println(none length);
