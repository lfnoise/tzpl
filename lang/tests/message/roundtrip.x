-- encode -> decode round-trips every SExpr case, including nesting and edges.
import sexprs.*;
import message.*;

fn rt(o SExpr) Void {
    let b = encode(o);
    print(o toString);
    print("  ==  ");
    print(decode(b) toString);
    print("   ok=");
    println(o toString == decode(b) toString);
}

rt(SExpr.bool(true));
rt(SExpr.bool(false));
rt(SExpr.int(0));
rt(SExpr.int(0 - 123456789));
rt(SExpr.float(3.25));
rt(SExpr.float(0.0 - 2.5));
rt(SExpr.symbol(toSymbol("foo")));
rt(SExpr.string(""));
rt(SExpr.string("hello, world"));
rt(SExpr.vec([SExpr]()));
rt(SExpr.vec([SExpr.int(1), SExpr.string("two"), SExpr.symbol(toSymbol("three"))]));
rt(SExpr.vec([SExpr.vec([SExpr.vec([SExpr.int(42)])])]));

-- symbol vs string: same text, different rendering + tag
let sym = encode(SExpr.symbol(toSymbol("x")));
let str = encode(SExpr.string("x"));
print("symbol renders bare: "); println(decode(sym) toString);
print("string renders quoted: "); println(decode(str) toString);
print("distinct tags: ");
println(tag(reader(sym)) == MsgTag.symbol && tag(reader(str)) == MsgTag.string);
