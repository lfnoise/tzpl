-- Match as expression (returning a value)

-- Match returning int
fn abs_match(x Int) Int {
    match (x) {
        n if (n < 0): return -n;
        n: return n;
    }
    return 0;
}
-5 abs_match println;
5 abs_match println;
0 abs_match println;

-- Match on strings
fn greet(lang String) String {
    match (lang) {
        "en": return "Hello";
        "es": return "Hola";
        "fr": return "Bonjour";
        _: return "Hi";
    }
    "Hi"
}
"en" greet println;
"es" greet println;
"fr" greet println;
"de" greet println;

-- Match on booleans
fn bool_to_str(b Bool) String {
    match (b) {
        true: return "yes";
        false: return "no";
    }
    "unknown"
}
true bool_to_str println;
false bool_to_str println;
