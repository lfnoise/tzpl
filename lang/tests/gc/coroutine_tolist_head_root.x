-- toList over a coroutine builds the head node from the first yield, then
-- resumes the coroutine again to peek at the second. That peek runs the VM,
-- so a GC cycle can run inside it, while the head node is held only by the
-- builtin. It must stay pinned: the list returned here used to be already
-- freed (the music player crashed reading it seconds later).

enum Cmd {
    on (Int, [Float]),
    off Int,
}

coro fn cmds(n Int) (Float, Cmd) {
    var i = 0;
    while (i < n) {
        -- a full collection between the first and second yield
        if (i == 1) { __gc_trace_cycle(); }
        yield (i toFloat, Cmd.on(i, [i toFloat, 0.5]));
        i = i + 1;
    }
}

fn describe(c Cmd) String {
    match (c) {
        on(k, ps): "on %^ %^" fmt(k, ps);
        off(k): "off %^" fmt(k);
    }
}

-- churn the heap so a freed head node would be reused
fn churn() Int {
    var total = 0;
    for (j : (1..2000)) { total = total + [j, j + 1, j + 2] length; }
    total
}

var lists = [List<(Float, Cmd)>]();
for (round : (1..20)) {
    lists push!(cmds(3) toList);
    churn();
}
for (l : lists) {
    let h = l head;
    "%^ %^" fmt(h.0, h.1 describe) println;
}
for (tc : lists[0]) { tc.1 describe println; }
