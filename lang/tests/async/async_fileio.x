-- Async file I/O builtins (NRT): readFileAsync / writeFileAsync & co.
-- The syscalls run on the NRTVM's I/O worker; a top-level await parks via
-- the host-wait hook. Output is deterministic because awaited values print
-- in program order. Uses a scratch dir under /tmp.
import std.futures.*;

let dir = "/tmp/tzpl_test_async_fileio";
makeDir(dir) println;

-- sync write, async readback
writeFile(dir $ "/a.txt", "hello async\n") println;
(await readFileAsync(dir $ "/a.txt")) unwrapOr("<none>") println;

-- async write, sync readback
(await writeFileAsync(dir $ "/w.txt", "written async\n")) println;
readFile(dir $ "/w.txt") unwrapOr("<none>") println;

-- async append, async readback
(await appendFileAsync(dir $ "/w.txt", "appended\n")) println;
(await readFileAsync(dir $ "/w.txt")) unwrapOr("<none>") println;

-- bytes round trip
var bb = bytes();
bb putU8!(65);
bb putU8!(66);
(await writeFileAsync(dir $ "/bin.dat", bb)) println;
match (await readFileBytesAsync(dir $ "/bin.dat")) {
    Option.some(b): { b byteLength println; b u8At(0) println; b u8At(1) println; }
    Option.none: println("read bytes failed");
}

-- error path: awaiting a read of a missing file yields none
(await readFileAsync(dir $ "/missing.txt")) isNone println;

-- two reads in flight at once, then a gather barrier
writeFile(dir $ "/one.txt", "one") println;
writeFile(dir $ "/two.txt", "two") println;
let fs = [readFileAsync(dir $ "/one.txt"), readFileAsync(dir $ "/two.txt")];
let vals = await gather(fs);
vals[0] unwrapOr("<none>") println;
vals[1] unwrapOr("<none>") println;

-- cleanup
removeFile(dir $ "/a.txt") println;
removeFile(dir $ "/w.txt") println;
removeFile(dir $ "/bin.dat") println;
removeFile(dir $ "/one.txt") println;
removeFile(dir $ "/two.txt") println;
