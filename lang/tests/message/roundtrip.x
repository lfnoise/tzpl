-- encode -> decode round-trips every Msg case, including nesting and edges.
import message.*;
import messageEncoding.*;

fn rt(o Msg) Void {
    let b = encode(o);
    print(o toString);
    print("  ==  ");
    print(decode(b) toString);
    print("   ok=");
    println(o toString == decode(b) toString);
}

rt(Msg.bool(true));
rt(Msg.bool(false));
rt(Msg.int(0));
rt(Msg.int(0 - 123456789));
rt(Msg.float(3.25));
rt(Msg.float(0.0 - 2.5));
rt(Msg.symbol(toSymbol("foo")));
rt(Msg.string(""));
rt(Msg.string("hello, world"));
rt(Msg.vec([Msg]()));
rt(Msg.vec([Msg.int(1), Msg.string("two"), Msg.symbol(toSymbol("three"))]));
rt(Msg.vec([Msg.vec([Msg.vec([Msg.int(42)])])]));

-- symbol vs string: same text, different rendering + tag
let sym = encode(Msg.symbol(toSymbol("x")));
let str = encode(Msg.string("x"));
print("symbol renders bare: "); println(decode(sym) toString);
print("string renders quoted: "); println(decode(str) toString);
print("distinct tags: ");
println(tag(reader(sym)) == MsgTag.symbol && tag(reader(str)) == MsgTag.string);
