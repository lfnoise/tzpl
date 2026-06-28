-- sexprs.x -- the SExpr value type, its printer, and constructors.

import strings.*;   -- separatedString, brackets, quotes (used by toString)

enum SExpr {
    bool Bool,
    int Int,
    float Float,
    symbol Symbol,
    string String,
    vec [SExpr],
}

fn toString(o SExpr) String {
    match (o) {
        SExpr.bool(b) : b ? "#t" : "#f";
        SExpr.int(i) : i toString;
        SExpr.float(i) : i toString;
        SExpr.symbol(s) : s toString;
        SExpr.string(s) : s quotes;
        SExpr.vec(v) : v @ toString separatedString(", ") brackets;
    }
}

fn sxpr(b Bool) SExpr = SExpr.bool(b);
fn sxpr(i Int) SExpr = SExpr.int(i);
fn sxpr(f Float) SExpr = SExpr.float(f);
fn sxpr(s Symbol) SExpr = SExpr.symbol(s);
fn sxpr(s String) SExpr = SExpr.string(s);
fn sxpr(v [SExpr]) SExpr = SExpr.vec(v);
