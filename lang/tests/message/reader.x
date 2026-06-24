-- Zero-copy reader: direct field access without decoding the whole tree.
import sexprs.*;
import message.*;

fn tagName(t MsgTag) String {
    match (t) {
        MsgTag.bool:   "bool";
        MsgTag.int:    "int";
        MsgTag.float:  "float";
        MsgTag.symbol: "symbol";
        MsgTag.string: "string";
        MsgTag.vec:    "vec";
    }
}

let e = SExpr.vec([
    SExpr.bool(false),
    SExpr.int(7),
    SExpr.float(1.5),
    SExpr.symbol(toSymbol("sym")),
    SExpr.string("str"),
    SExpr.vec([SExpr.int(10), SExpr.int(20), SExpr.int(30)]),
]);
let b = encode(e);
let r = reader(b);

println(childCount(r));
println(asBool(child(r, 0)));
println(asInt(child(r, 1)));
println(asFloat(child(r, 2)));
println(asStr(child(r, 3)));
println(asStr(child(r, 4)));

-- reach into the nested vec directly (no full decode), O(1) per child
let nested = child(r, 5);
println(childCount(nested));
println(asInt(child(nested, 2)));

-- iterate children, printing tags only
for (c : children(r)) {
    print(tagName(tag(c)));
    print(" ");
}
println("");
