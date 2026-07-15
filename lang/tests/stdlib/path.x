-- std.path: pure path-string manipulation.
import std.path.*;

"/a/b/c.txt" basename println;
"/a/b/" basename println;
"file" basename println;
"/a/b/c.txt" dirname println;
"file" dirname println;
"/x" dirname println;
"/" dirname println;
"a/b.txt" extension println;
"a.b/c" extension println;
"a/.hidden" extension println;
"a/b.txt" withExtension(".md") println;
"a/b" withExtension(".md") println;
joinPath("a", "b") println;
joinPath("a/", "b") println;
joinPath("a", "/b") println;
joinPath("", "b") println;
joinPath("a", "") println;
"/a//b/c" splitPath println;
"/a" isAbsolute println;
"a" isAbsolute println;
