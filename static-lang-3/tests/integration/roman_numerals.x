-- Roman Numerals Encode (Rosetta Code)
-- Fold over value/symbol pairs to build the roman numeral.
-- Recursive subtraction for each denomination.

fn repeatStr(s String, n Int) String {
    if (n <= 0) { "" }
    else { s $ repeatStr(s, n - 1) }
}

fn toRoman(n Int) String {
    let values = [1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1];
    let symbols = ["M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"];

    -- Fold over zip(values, symbols), accumulating (result_string, remaining_value)
    let pairs = zip(values, symbols);
    let final = pairs fold(("", n), fn(state (String, Int), pair (Int, String)) (String, Int) {
        let count = state.1 // pair.0;
        (state.0 $ repeatStr(pair.1, count), state.1 - count * pair.0)
    });
    final.0
}

-- Auto-map toRoman over test values
[1, 4, 9, 14, 42, 99, 2024, 1987, 3999] toRoman println;
