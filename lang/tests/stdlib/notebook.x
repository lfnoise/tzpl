-- std.notebook: programmatic .tzd construction. Checks the Msg layout,
-- the TZB header, and a write/read-back round trip through /tmp.
import std.message.*;
import std.messageEncoding.*;
import std.notebook.*;

let cells = [
    proseCell("# Demo\nA generated notebook."),
    codeCell("import ui.*;\npanel(\"main\");\nslider(\"freq\", 20.0, 2000.0, 440.0);",
             true, "setup"),
    panelCell("main", 160.0),
    codeCell("println(\"hello\");"),
    presetsCell("bank"),
];

-- Structure: the exact record trees the app's loader reads.
cells notebookMsg toString println;

-- Encoded form carries a valid TZB header.
let b = cells notebookBytes;
b isMessage println;

-- decode(encode(x)) preserves the document tree.
println(decode(b) toString == cells notebookMsg toString);

-- File round trip.
let dir = "/tmp/tzpl_test_stdlib_notebook";
makeDir(dir) println;
saveNotebook(dir $ "/gen.tzd", cells) println;
match (readFileBytes(dir $ "/gen.tzd")) {
    Option.some(rb): println(decode(rb) toString == cells notebookMsg toString);
    Option.none: println("read back failed");
}
