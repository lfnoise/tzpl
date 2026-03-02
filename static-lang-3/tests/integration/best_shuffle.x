-- Best Shuffle (Rosetta Code)
-- Shuffle so minimal characters remain in original position.
-- Recursive swap search instead of loops.

fn strToChars(s String) [String] {
    if (s length == 0) {
        let empty [String] = [];
        empty
    }
    else { [substring(s, 0, 1)] $ strToChars(substring(s, 1, s length - 1)) }
}

fn charsToStr(chars [String]) String =
    chars fold("", fn(a String, b String) String { a $ b });

fn copyArr(arr [String]) [String] =
    arr map(fn(s String) String { s });

fn setAt(arr [String], idx Int, val String) [String] =
    arr enumerate map(fn(p (Int, String)) String { if (p.0 == idx) { val } else { p.1 } });

fn trySwap(result [String], chars [String], i Int, j Int, n Int) [String] {
    if (j >= n) { result }
    else if (i != j && result[i] == chars[i] && result[j] != chars[i] && result[i] != chars[j]) {
        let tmp = result[i];
        let r2 = setAt(result, i, result[j]);
        setAt(r2, j, tmp)
    }
    else { trySwap(result, chars, i, j + 1, n) }
}

fn shuffleFrom(result [String], chars [String], i Int, n Int) [String] {
    if (i >= n) { result }
    else { shuffleFrom(trySwap(result, chars, i, 0, n), chars, i + 1, n) }
}

fn countFixed(result [String], chars [String]) Int {
    zip(result, chars)
        map(fn(p (String, String)) Int { if (p.0 == p.1) { 1 } else { 0 } })
        fold(0, fn(a Int, b Int) Int { a + b })
}

fn bestShuffle(s String) (String, Int) {
    let chars = s strToChars;
    let orig = chars copyArr;
    let result = shuffleFrom(chars copyArr, orig, 0, orig length);
    (result charsToStr, countFixed(result, orig))
}

let words = ["abracadabra", "seesaw", "elk", "grrrrrr", "up", "a"];
words map(fn(w String) Int { bestShuffle(w).1 }) println;
