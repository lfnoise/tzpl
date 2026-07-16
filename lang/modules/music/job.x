-- job.x -- hierarchical collections and behaviors, inspired by HMSL's
-- players, collections, and behavior abstraction.
--
-- A Coll arranges Shapes hierarchically: sequential, parallel, repeated,
-- or behavior-selected. `realize` walks the hierarchy and produces the
-- shared List<Event>; behaviors (and any random shape content) are rolled
-- AT REALIZE TIME, so realizing the same Coll twice gives two different
-- readings when randomness is involved -- the analog of an HMSL job is a
-- Coll re-realized each pass:
--
--     let form = Coll.seqc([intro, Coll.rpt((4, Coll.sel((Behavior.shuffled, riffs)))), coda]);
--     play(realize(form), v);                        -- one reading
--     p replace(realize(form));                      -- another
--
-- Selection state (inOrder cursors, shuffle pools) is per-realize and keyed
-- by the node's position in the tree, so a sel node visited repeatedly
-- (e.g. under rpt) advances/exhausts across visits.
--
-- RT-safe: pure (RNG only).

export music.shape.*;

enum Coll {
    leaf Shape,
    seqc [Coll],              -- children one after another
    parc [Coll],              -- children together
    rpt (Int, Coll),          -- child n times (re-realized each pass)
    sel (Behavior, [Coll]),   -- behavior picks one child per visit
}

enum Behavior {
    inOrder,            -- cycle children in order
    atRandom,           -- uniform random child
    shuffled,           -- random without repeat until all seen, then reshuffle
    weighted [Float],   -- random child by weight
}

-- Realize-pass state: cursors for inOrder, remaining pools for shuffled,
-- keyed by tree position.
struct _RState {
    cursors [Int: Int],
    pools [Int: [Int]],
}

fn _select(b Behavior, n Int, key Int, st _RState) Int {
    match (b) {
        Behavior.inOrder: {
            let cur = get(st.cursors, key, 0);
            st.cursors[key] = cur + 1;
            cur % n
        }
        Behavior.atRandom: irand(0, n - 1);
        Behavior.shuffled: {
            var pool = get(st.pools, key, [Int]());
            if (pool length == 0) {
                var fresh = [Int]();
                var i = 0;
                while (i < n) { fresh push!(i); i = i + 1; }
                pool = fresh muss;
            }
            let idx = pool[pool length - 1];
            pool pop!;
            st.pools[key] = pool;
            idx
        }
        Behavior.weighted(ws): {
            var idxs = [Int]();
            var i = 0;
            while (i < n) { idxs push!(i); i = i + 1; }
            wpick(idxs, ws)
        }
    }
}

-- returns (events, end beat)
fn _realize(c Coll, t0 Float, key Int, st _RState) (List<Event>, Float) {
    match (c) {
        Coll.leaf(sh): (shapeEvents(sh, t0), t0 + shapeDur(sh));
        Coll.seqc(kids): {
            var es List<Event> = nil;
            var t = t0;
            var i = 0;
            while (i < kids length) {
                let r = _realize(kids[i], t, key * 31 + i + 1, st);
                es = cat(es, r.0);
                t = r.1;
                i = i + 1;
            }
            (es, t)
        }
        Coll.parc(kids): {
            var es List<Event> = nil;
            var end = t0;
            var i = 0;
            while (i < kids length) {
                let r = _realize(kids[i], t0, key * 31 + i + 1, st);
                es = merge(es, r.0);
                if (r.1 > end) { end = r.1; }
                i = i + 1;
            }
            (es, end)
        }
        Coll.rpt(p): {
            let (n, kid) = p;
            var es List<Event> = nil;
            var t = t0;
            var i = 0;
            while (i < n) {
                let r = _realize(kid, t, key * 31 + 1, st);
                es = cat(es, r.0);
                t = r.1;
                i = i + 1;
            }
            (es, t)
        }
        Coll.sel(p): {
            let (b, kids) = p;
            let idx = _select(b, kids length, key, st);
            _realize(kids[idx], t0, key * 31 + idx + 1, st)
        }
    }
}

-- Realize the hierarchy into events starting at t0. Behaviors re-roll on
-- every call.
fn realize(c Coll, t0 Float = 0.0) List<Event> {
    var cur [Int: Int] = [:];
    var pools [Int: [Int]] = [:];
    var st = _RState { cursors: cur, pools: pools };
    let r = _realize(c, t0, 1, st);
    r.0
}

-- End beat of one realization (rolls behaviors like realize does).
fn realizeDur(c Coll, t0 Float = 0.0) Float {
    var cur [Int: Int] = [:];
    var pools [Int: [Int]] = [:];
    var st = _RState { cursors: cur, pools: pools };
    let r = _realize(c, t0, 1, st);
    r.1 - t0
}
