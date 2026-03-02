-- Match with guard clauses

-- Basic guards
fn classify(x Int) Void {
    match (x) {
        n if (n > 0): "positive" println;
        n if (n < 0): "negative" println;
        0: "zero" println;
        _: "unreachable" println;
    }
}
classify(10);
classify(-5);
classify(0);

-- Guards with enum
enum Option {
    some Int,
    none
}

fn describe(opt Option) Void {
    match (opt) {
        Option.some(n) if (n > 0): "positive value" println;
        Option.some(n) if (n < 0): "negative value" println;
        Option.some(0): "zero value" println;
        Option.some(n): "some value" println;
        Option.none: "nothing" println;
    }
}
describe(Option.some(5));
describe(Option.some(-3));
describe(Option.some(0));
describe(Option.none);

-- Guards on range
fn range_classify(n Int) String {
    match (n) {
        x if (x >= 90): return "A";
        x if (x >= 80): return "B";
        x if (x >= 70): return "C";
        _: return "F";
    }
    "F"
}
95 range_classify println;
85 range_classify println;
75 range_classify println;
60 range_classify println;
