-- Regression: coroutine ARGUMENTS that are multi-word Inline composites.
-- CoroutineObj.args_ used to copy one word per argument, losing the trailing
-- words of an inline struct/tuple arg (reads of its pointer fields crashed).
-- Covers op_coro_create + op_coro_resume and the toList drain path.

struct Ev2 { t Float, ps [Float] }        -- 2-word inline struct (Float + Obj*)

coro fn fields(e Ev2) Float {
    yield e.t;
    yield e.ps length toFloat;
    yield e.ps[0];
}

let e = Ev2 { t: 1.5, ps: [10.0, 20.0] };
let c = fields(e);
c next println;
c next println;
c next println;
c next println;

-- drained via toList (syncResumeCoroutineInto path)
fields(e) toList collect(3) println;

-- inline struct arg used by a sync fn called from inside the coroutine
fn total(e2 Ev2) Float {
    var s = e2.t;
    for (x : e2.ps) { s = s + x; }
    s
}
coro fn viaCall(e2 Ev2) Float {
    yield total(e2);
    yield e2.t;
}
viaCall(e) toList collect(2) println;

-- tuple arg (2-word inline) + a second 1-word arg after it
coro fn pair(p (Float, Int), k Int) Int {
    yield p.1 + k;
    yield p.0 toInt;
}
pair((2.5, 40), 2) toList collect(2) println;

-- mixed: 1-word arg BEFORE the inline arg (offset shifting)
coro fn shifted(k Int, e2 Ev2) Float {
    yield k toFloat + e2.t;
    yield e2.ps[1];
}
shifted(10, e) toList collect(2) println;
