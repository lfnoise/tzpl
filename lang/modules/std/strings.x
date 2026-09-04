-- strings.x -- string utilities: wrapping, padding, line splitting,
-- prefix/suffix stripping, and glob matching. Pure and RT-safe.

fn parens(s String)   String = "(%^)" fmt(s);
fn brackets(s String) String = "[%^]" fmt(s);
fn braces(s String)   String = "{%^}" fmt(s);
fn quotes(s String)   String = "\"%^\"" fmt(s);

-- Join `strings` with `separator` between each element.
fn separatedString(strings [String], separator String = " ") String {
    var out = "";
    var between = false;
    for (s : strings) {
        if (between) {
            out = out $ separator;
        } else {
            between = true;
        }
        out = out $ s;
    }
    out
}

-- `s` repeated `n` times ("" for n <= 0).
fn repeatString(s String, n Int) String {
    var out = "";
    var i = 0;
    while (i < n) {
        out = out $ s;
        i = i + 1;
    }
    out
}

-- Pad on the left with `pad` until at least `width` codepoints long.
-- Exact when `pad` is a single codepoint.
fn padStart(s String, width Int, pad String = " ") String {
    let deficit = width - s length;
    deficit <= 0 ? s : pad repeatString(deficit) $ s
}

-- Pad on the right with `pad` until at least `width` codepoints long.
fn padEnd(s String, width Int, pad String = " ") String {
    let deficit = width - s length;
    deficit <= 0 ? s : s $ pad repeatString(deficit)
}

-- Split into lines: "\r\n" and "\r" normalize to "\n"; a trailing newline
-- does not produce an empty last line.
fn splitLines(s String) [String] {
    let parts = s replace("\r\n", "\n") replace("\r", "\n") split("\n");
    let n = parts length;
    n > 0 && parts[n - 1] == "" ? parts take(n - 1) : parts
}

fn lines(s String) [String] = s splitLines;

-- Uppercase the first character (ASCII).
fn capitalize(s String) String {
    s length == 0
        ? s
        : s substring(0, 1) toUpper $ s substring(1, s length - 1)
}

-- `s` without the prefix / suffix, unchanged if it does not match.
fn stripPrefix(s String, prefix String) String {
    s startsWith(prefix)
        ? s substring(prefix length, s length - prefix length)
        : s
}

fn stripSuffix(s String, suffix String) String {
    s endsWith(suffix) && suffix length > 0
        ? s substring(0, s length - suffix length)
        : s
}

fn equalsIgnoreCase(a String, b String) Bool = a toLower == b toLower;

-- Materialize a lazy List<Int> (e.g. codePoints) as an indexable array.
fn _toIntArray(xs List<Int>) [Int] = [Int]() append!(xs);

-- Match one pattern element at p[pi] against codepoint c.
-- Returns (matched, index just past the element). Elements are a literal
-- codepoint, '?' (any one), or a class "[abc]" / "[a-z]" / "[!abc]".
fn _globOne(p [Int], pi Int, c Int) (Bool, Int) {
    if (p[pi] != 91) {                       -- not '['
        return (p[pi] == 63 || p[pi] == c, pi + 1);   -- '?' or literal
    }
    var i = pi + 1;
    var negate = false;
    if (i < p length && p[i] == 33) {        -- '!'
        negate = true;
        i = i + 1;
    }
    var matched = false;
    var first = true;
    while (i < p length && (first || p[i] != 93)) {   -- ']' literal if first
        if (i + 2 < p length && p[i + 1] == 45 && p[i + 2] != 93) {  -- "a-z"
            if (c >= p[i] && c <= p[i + 2]) { matched = true; }
            i = i + 3;
        } else {
            if (p[i] == c) { matched = true; }
            i = i + 1;
        }
        first = false;
    }
    let after = i < p length ? i + 1 : i;    -- step past ']'
    (negate ? !matched : matched, after)
}

-- Glob match: '*' any run (including empty), '?' any one codepoint,
-- "[abc]" / "[a-z]" / "[!abc]" character classes. Whole-string match:
-- "foo.txt" glob("*.txt") is true.
fn glob(s String, pattern String) Bool {
    let p = pattern codePoints _toIntArray;
    let t = s codePoints _toIntArray;
    var pi = 0;
    var ti = 0;
    var starPi = -1;   -- pattern index just past the last '*'
    var starTi = 0;    -- text index where that '*' match started
    while (ti < t length) {
        var stepped = false;
        if (pi < p length) {
            if (p[pi] == 42) {               -- '*'
                pi = pi + 1;
                starPi = pi;
                starTi = ti;
                stepped = true;
            } else {
                let (matched, afterPi) = _globOne(p, pi, t[ti]);
                if (matched) {
                    pi = afterPi;
                    ti = ti + 1;
                    stepped = true;
                }
            }
        }
        if (!stepped) {
            if (starPi < 0) { return false; }
            starTi = starTi + 1;             -- widen the last '*' by one
            ti = starTi;
            pi = starPi;
        }
    }
    while (pi < p length && p[pi] == 42) { pi = pi + 1; }
    pi == p length
}
