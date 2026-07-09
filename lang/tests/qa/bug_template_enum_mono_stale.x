-- QA: a template enum constructed inside a template fn must be re-monomorphized
-- for each monomorphization of that fn.
--
-- inferExpr's EnumConstructor case used to reuse the node's cached resolvedType,
-- which recheckTemplateBody had filled in for whichever mono ran first.  The
-- second mono of `force` then saw ThunkCell<Int> instead of ThunkCell<String>.
-- Both node shapes were affected: the explicit-type-arg form (a parser-built
-- EnumConstructExpr) and the inferred form (a re-tagged CallExpr_).

enum ThunkCell<T> {
    value T,
    thunk () T,
}

type Thunk<T> = Ref<ThunkCell<T>>;

fn thunk<T>(f () T) Thunk<T> = &ThunkCell<T>.thunk(f);

-- explicit type args: ThunkCell<T>.value(v)
fn force<T>(r Thunk<T>) T {
    match (*r) {
        ThunkCell.value(v) : v;
        ThunkCell.thunk(f) : {
            let v = f();
            r <- ThunkCell<T>.value(v);
            v
        }
    }
}

-- inferred type args: ThunkCell.value(v)
fn forceInferred<T>(r Thunk<T>) T {
    match (*r) {
        ThunkCell.value(v) : v;
        ThunkCell.thunk(f) : {
            let v = f();
            r <- ThunkCell.value(v);
            v
        }
    }
}

-- Int mono first, then String: the second must not inherit the first's type.
let a = fn() Int { 1 + 1 } thunk;
a force println;

let b = fn() String { "ok" } thunk;
b force println;

let c = fn() Float { 3.5 } thunk;
c force println;

-- same, through the inferred-type-arg constructor
let d = fn() Int { 7 } thunk;
d forceInferred println;

let e = fn() String { "yes" } thunk;
e forceInferred println;

-- forcing memoizes: the cell is rewritten to .value, and f runs exactly once
var count = &0;
let m = fn() Int { count <- *count + 1; 41 + *count } thunk;
m force println;
m force println;
println("evals: " $ (*count) toString);
m println;
