-- Branch constructs whose taken paths all end in an explicit return, as the
-- last statement of a function: the branch-exit jumps target the function
-- epilogue, which must exist even though the last emitted op is already a
-- return. Regression: the epilogue was skipped and those jumps resolved one
-- past the end of the CodeBlock.

-- if without else, then-branch returns; the false path exits through the
-- epilogue.
fn ifReturn(x Int) Void {
    if (x > 0) {
        println("positive");
        return;
    }
}

-- statement-form match, every arm returns; a (statically impossible here)
-- fall-through exits through the epilogue.
enum E { a, b }
fn matchReturn(x E) Void {
    match (x) {
        E.a: { println("a"); return; }
        E.b: { println("b"); return; }
    }
}

-- exhaustive match-as-expression over an enum: the last case still emits a
-- tag test; its fail path must land on a real instruction.
fn name(x E) String = match(x) {
    E.a: "a";
    E.b: "b";
};

ifReturn(1);
ifReturn(0);
ifReturn(-1);
matchReturn(E.a);
matchReturn(E.b);
println(name(E.a));
println(name(E.b));
println("done");
