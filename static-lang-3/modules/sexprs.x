---------------------------------------------------------------------

enum SExpr {
    bool Bool,
    int Int,
    float Float,
    symbol String,
    string String,
    vec [SExpr],
}

fn toString(o SExpr) String {
    match (o) {
        SExpr.bool(b) : b ? "#t" : "#f";
        SExpr.int(i) : i toString;
        SExpr.float(i) : i toString;
        SExpr.symbol(s) : s toString;
        SExpr.string(s) : s braces(Braces.quotes);
        SExpr.vec(v) : v separatedString(", ") braces(Braces.square);
    }
}

fn sxpr(b Bool) SExpr = SExpr.bool(b);
fn sxpr(i Int) SExpr = SExpr.int(b);
fn sxpr(f Float) SExpr = SExpr.float(b);
fn sxpr(s Symbol) SExpr = SExpr.symbol(b);
fn sxpr(s String) SExpr = SExpr.string(b);
fn sxpr(v [SExpr]) SExpr = SExpr.vec(v);


---------------------------------------------------------------------

fn parens(s String) String = "(%^)" fmt(s);
fn brackets(s String) String = "[%^]" fmt(s);
fn braces(s String) String = "{%^}" fmt(s);
fn quotes(s String) String = "\"%^\"" fmt(s);

fn separatedString(strings [String], separator String = " ") String {
    var out = "";
    var between = false;
    for (s : strings) {
        if (between) {
            out = out + separator;
        } else {
            between = true;
        }
        out = out + s;
    }
    out
}

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





