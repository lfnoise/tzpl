-- fs.x -- ergonomic wrappers over the NRT file builtins (readFile,
-- writeFile, listDir, ...). Everything in this module performs blocking
-- syscalls and is NOT real-time safe. Pure path-string helpers live in
-- std.path (RT-safe).
--
-- Builtins return Option; the Result variants here add a path-bearing
-- error message.

import std.result.*;
import std.strings.*;   -- splitLines

-- The file's contents, or `default` if it cannot be read.
fn readFileOr(path String, default String) String = readFile(path) unwrapOr(default);

-- The file's contents split into lines ("\r\n" normalized, trailing
-- newline not counted as an empty last line).
fn readLines(path String) Option<[String]> {
    match (readFile(path)) {
        Option.some(s): Option.some(s splitLines);
        Option.none: Option<[String]>.none;
    }
}

-- Write lines joined with "\n", with a trailing newline.
fn writeLines(path String, lines [String]) Bool {
    var out = "";
    for (line : lines) { out = out $ line $ "\n"; }
    writeFile(path, out)
}

-- Result variants: the err carries the failing path.

fn readFileResult(path String) Result<String, String> {
    match (readFile(path)) {
        Option.some(s): Result<String, String>.ok(s);
        Option.none: Result<String, String>.err("cannot read file: " $ path);
    }
}

fn readFileBytesResult(path String) Result<Bytes, String> {
    match (readFileBytes(path)) {
        Option.some(b): Result<Bytes, String>.ok(b);
        Option.none: Result<Bytes, String>.err("cannot read file: " $ path);
    }
}

fn writeFileResult(path String, contents String) Result<Bool, String> {
    writeFile(path, contents)
        ? Result<Bool, String>.ok(true)
        : Result<Bool, String>.err("cannot write file: " $ path)
}

fn listDirResult(path String) Result<[String], String> {
    match (listDir(path)) {
        Option.some(names): Result<[String], String>.ok(names);
        Option.none: Result<[String], String>.err("cannot list directory: " $ path);
    }
}
