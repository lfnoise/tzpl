-- path.x -- pure path-string manipulation. No syscalls: everything here is
-- real-time safe; the filesystem itself lives in std.fs (NRT).
--
-- Paths are byte strings with '/' separators; offsets from indexOf /
-- lastIndexOf compose with substring's byte indexing.

-- Everything after the last '/', or the whole path if there is none.
-- A trailing '/' is ignored: basename("/a/b/") == "b".
fn basename(p String) String {
    var s = p;
    while (s length > 1 && s endsWith("/")) {
        s = s substring(0, s length - 1);
    }
    match (s lastIndexOf("/")) {
        Option.some(i): s substring(i + 1, s length - i - 1);
        Option.none: s;
    }
}

-- Everything before the last '/': dirname("/a/b/c.txt") == "/a/b".
-- A path with no '/' has dirname "."; the root's dirname is "/".
fn dirname(p String) String {
    var s = p;
    while (s length > 1 && s endsWith("/")) {
        s = s substring(0, s length - 1);
    }
    match (s lastIndexOf("/")) {
        Option.some(i): i == 0 ? "/" : s substring(0, i);
        Option.none: ".";
    }
}

-- The extension of the basename including the dot ("" if none):
-- extension("a/b.txt") == ".txt", extension("a.b/c") == "".
fn extension(p String) String {
    let name = p basename;
    match (name lastIndexOf(".")) {
        Option.some(i): i == 0 ? "" : name substring(i, name length - i);
        Option.none: "";
    }
}

-- Replace (or add) the extension: withExtension("a/b.txt", ".md") == "a/b.md".
fn withExtension(p String, ext String) String {
    let old = p extension;
    p substring(0, p length - old length) $ ext
}

-- Join two path segments with exactly one separator between them.
-- An absolute right side replaces the left: joinPath("a", "/b") == "/b".
fn joinPath(a String, b String) String {
    if (a == "" || b isAbsolute) { return b; }
    if (b == "") { return a; }
    a endsWith("/") ? a $ b : a $ "/" $ b
}

-- Split into components; "" components from doubled or leading '/' are
-- dropped: splitPath("/a//b/c") == ["a", "b", "c"].
fn splitPath(p String) [String] {
    p split("/") filter(fn(s String) Bool { s != "" })
}

fn isAbsolute(p String) Bool = p startsWith("/");
