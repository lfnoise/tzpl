-- File/OS builtins + std.fs wrappers (NRT). Uses a scratch dir under /tmp.
import std.fs.*;
import std.result.*;

let dir = "/tmp/tzpl_test_stdlib_fs";
makeDir(dir) println;
makeDir(dir $ "/nested/deep") println;

writeFile(dir $ "/a.txt", "hello\nworld\n") println;
appendFile(dir $ "/a.txt", "more\n") println;
readFile(dir $ "/a.txt") unwrapOr("<none>") println;
readFile(dir $ "/missing.txt") unwrapOr("<none>") println;

fileExists(dir $ "/a.txt") println;
isDirectory(dir) println;
isDirectory(dir $ "/a.txt") println;
fileSize(dir $ "/a.txt") unwrapOr(-1) println;
fileSize(dir $ "/missing.txt") unwrapOr(-1) println;

writeLines(dir $ "/l.txt", ["one", "two"]) println;
readLines(dir $ "/l.txt") unwrap println;
readFileOr(dir $ "/nope.txt", "dflt") println;
readFileResult(dir $ "/l.txt") isOk println;
readFileResult(dir $ "/nope.txt") errOption unwrap println;

listDir(dir) unwrap println;
renameFile(dir $ "/a.txt", dir $ "/b.txt") println;
fileExists(dir $ "/a.txt") println;

-- Bytes round-trip
var bb = bytes();
bb putU8!(65);
bb putU8!(66);
writeFile(dir $ "/bin.dat", bb) println;
match (readFileBytes(dir $ "/bin.dat")) {
    Option.some(b): { b byteLength println; b u8At(0) println; b u8At(1) println; }
    Option.none: println("read bytes failed");
}

-- env / process
getEnv("TZPL_TEST_ENV_UNSET_12345") isNone println;
programArgs() println;

-- cleanup
removeFile(dir $ "/b.txt") println;
removeFile(dir $ "/l.txt") println;
removeFile(dir $ "/bin.dat") println;
listDir(dir) unwrap println;
