-- strings.x -- small string-formatting utilities.

fn parens(s String)   String = "(%^)" fmt(s);
fn brackets(s String) String = "[%^]" fmt(s);
fn braces(s String)   String = "{%^}" fmt(s);
fn quotes(s String)   String = "\"%^\"" fmt(s);

-- Join `strings` with `separator` between each element.
fn separatedString(strings [String], separator String = " ") String {
    var out = "";
    var between = false;
    for (s : strings) {
        if (between) {
            out = out $ separator;
        } else {
            between = true;
        }
        out = out $ s;
    }
    out
}
