-- std.fs async Result wrappers + postfix `try` across await boundaries.
import std.fs.*;
import std.result.*;

let dir = "/tmp/tzpl_test_async_fileio_try";
makeDir(dir) println;
writeFile(dir $ "/ok.txt", "content") println;

-- `try` on an awaited Result inside an async fn: the err early-return
-- must resolve the Future (op_async_return), same as async_try.x.
async fn readTagged(path String) Result<String, String> {
    let s = (await readFileAsyncResult(path)) try;
    Result<String, String>.ok("got: " $ s)
}

fn report(r Result<String, String>) Void {
    match (r) {
        ok(s): println(s);
        err(m): println(m);
    }
}

report(await readTagged(dir $ "/ok.txt"));
report(await readTagged(dir $ "/missing.txt"));

-- writeFileAsyncResult: ok path, then err path (dir does not exist)
(await writeFileAsyncResult(dir $ "/w.txt", "x")) isOk println;
(await writeFileAsyncResult(dir $ "/no_such_dir/w.txt", "x")) errOption unwrap println;

-- cleanup
removeFile(dir $ "/ok.txt") println;
removeFile(dir $ "/w.txt") println;
