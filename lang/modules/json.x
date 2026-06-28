-- json.x -- a minimal JSON value type and printer.

import strings.*;   -- separatedString, braces

enum Json {
    null,
    bool Bool,
    number Float,
    string String,
    array [Json]
    object [String:Json]
}

fn toString(o Json) String {
    match (o) {
        Json.null: "null";
        Json.bool(true): "true";
        Json.bool(false): "false";
        Json.number(x): x toString;
        Json.string(s): "\"" $ s $ "\"";
        Json.array(a): a @ toString separatedString(", ") braces;
        Json.object(m): m pairs map(fn(p) { "\"%^\": %^" fmt(p.0, p.1) }) separatedString(", ") braces;
    }
}
