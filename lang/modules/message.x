-- message.x -- the Msg value type, its printer, and constructors.

import strings.*;   -- separatedString, brackets, quotes (used by toString)

enum Msg {
    bool Bool,
    int Int,
    float Float,
    symbol Symbol,
    string String,
    vec [Msg],
}

fn toString(o Msg) String {
    match (o) {
        Msg.bool(b) : b ? "#t" : "#f";
        Msg.int(i) : i toString;
        Msg.float(i) : i toString;
        Msg.symbol(s) : s toString;
        Msg.string(s) : s quotes;
        Msg.vec(v) : v @ toString separatedString(", ") brackets;
    }
}

-- asMsg lifts any AsMsg-conforming value into a Msg. The overloads cover the
-- leaf cases directly; the existential overload converts a homogeneous vec of
-- any conforming element type (including nested vecs) element by element.
constraint AsMsg<T> = requires { asMsg(T) Msg };

fn asMsg(s Msg) Msg = s;
fn asMsg(b Bool) Msg = Msg.bool(b);
fn asMsg(i Int) Msg = Msg.int(i);
fn asMsg(f Float) Msg = Msg.float(f);
fn asMsg(s Symbol) Msg = Msg.symbol(s);
fn asMsg(s String) Msg = Msg.string(s);
fn asMsg(v [Msg]) Msg = Msg.vec(v);

-- Per-element conversion is routed through a scalar `some AsMsg` parameter so
-- the witness dispatch fires per element. Explicit `@`-mapping the constraint
-- method directly (`v @ asMsg`) does not dispatch through the existential
-- witness; auto-mapping over a scalar existential parameter does.
fn _asMsgElem(x some AsMsg) Msg = asMsg(x);
fn asMsg(v [some AsMsg]) Msg = Msg.vec(v @ _asMsgElem);
