-- silo.x -- orchestration helpers for loading and synchronized-starting the
-- parts of a session across silos. Built on the async/await load barrier
-- (siloLoad -> Future) + siloStartAt. This is the orchestration layer (main/NRT
-- VM): it builds graphs, loads task code, and starts tasks on a common beat.
-- The tasks themselves run on the silos' RT VMs (their own coroutines).

import audio_engine.*;
import std.futures.*;

-- The result of preparing a part: ok, or an error message from compile/load.
enum LoadResult {
    ok,
    err String,
}

fn isOk(r LoadResult) Bool {
    match (r) {
        ok: true;
        err(e): false;
    }
}
fn error(r LoadResult) String {
    match (r) {
        ok: "";
        err(e): e;
    }
}

-- Round `beat` up to the next multiple of `grid` (the next bar line).
fn quantizeUp(beat Float, grid Float) Float {
    ((beat / grid) floor + 1.0) * grid
}

-- Prepare a part on `silo`: attach its VM and load the task module `source`
-- (which defines the part's task coroutine(s) and a start() entry). Returns once
-- the load has landed on the silo (the await barrier) -- ok, or an error.
async fn prepare(silo Int, source String) LoadResult {
    attachVM(silo);
    let err = await siloLoad(silo, source);
    if (err length > 0) { LoadResult.err(err) } else { LoadResult.ok }
}

-- Load a silo task module from a .x file. Returns the load completion handle
-- (await it like siloLoad). The file should define the task coroutine(s) and a
-- start() entry, and `import audio_engine.*` for spawn / playNote / releaseNote.
fn siloLoadFile(silo Int, path String) Future<String> {
    siloLoad(silo, readFile(path))
}
