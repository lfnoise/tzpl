-- Postfix `try` on Option<T>: unwraps some, early-returns none.

-- Builtin-produced Option (parseInt)
fn parsedPlus(s String, d Int) Option<Int> {
    let n = parseInt(s) try;
    Option.some(n + d)
}

println(parsedPlus("40", 2));
println(parsedPlus("zz", 2));

-- Pointer-payload Option (indexOf returns Option<Int>; readLines-style String payloads)
fn firstWordUpper(s String) Option<String> {
    let ix = s indexOf(" ") try;
    Option.some(s substring(0, ix) toUpper)
}

println(firstWordUpper("hello world"));
println(firstWordUpper("nospace"));

-- Two tries chained through arithmetic
fn sumParsed(a String, b String) Option<Int> {
    Option.some(parseInt(a) try + parseInt(b) try)
}
println(sumParsed("1", "2"));
println(sumParsed("1", "x"));
println(sumParsed("x", "2"));

-- try inside a typed lambda: propagates out of the LAMBDA only
fn mapParse(xs [String]) [Option<Int>] {
    xs map(fn(s String) Option<Int> {
        let n = parseInt(s) try;
        Option.some(n * 10)
    })
}
println(mapParse(["1", "no", "3"]));

-- nested functions: try binds to the innermost enclosing fn
fn outer(s String) Option<Int> {
    fn inner(t String) Option<Int> {
        let n = parseInt(t) try;
        Option.some(n + 1)
    }
    match (inner(s)) {
        Option.some(v): Option.some(v * 2);
        Option.none: Option.some(-1);   -- inner's none does NOT propagate out of outer
    }
}
println(outer("10"));
println(outer("bad"));
