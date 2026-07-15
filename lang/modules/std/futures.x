-- futures.x -- async/await combinators for Tzopilotl.
--
-- Built entirely on the language's async/await core (`async fn` / `await` /
-- `Future<T>` / `delay`). Because the async event loop is single-threaded and
-- drained in virtual-beat order, awaiting a set of already-spawned futures one
-- at a time still completes only once the LAST of them resolves -- so these need
-- no native support. Spawn the work first (each `async fn` call runs eagerly to
-- its first suspension and hands back a Future), then combine the futures here.
--
-- Example:
--     let parts = [loadPart("a"), loadPart("b"), loadPart("c")];  -- all in flight
--     await awaitAll(parts);                                      -- one barrier

-- Wait for every future to resolve. Returns when the last one completes.
async fn awaitAll<T>(fs [Future<T>]) Void {
    var i = 0;
    while (i < fs length) {
        await fs[i];
        i = i + 1;
    }
}

-- Wait for every future and collect their values, in the order of `fs`
-- (independent of the order in which they resolved).
async fn gather<T>(fs [Future<T>]) [T] {
    var out [T] = [];
    var i = 0;
    while (i < fs length) {
        out push!(await fs[i]);
        i = i + 1;
    }
    out
}
