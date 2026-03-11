-- FizzBuzz (Rosetta Code)
-- Auto-map a fizzbuzz function over the range 1..20.
-- Uses recursive intToStr to convert non-fizzbuzz numbers to strings.

fn digitChar(d Int) String {
    match (d) {
        0: "0"; 1: "1"; 2: "2"; 3: "3"; 4: "4";
        5: "5"; 6: "6"; 7: "7"; 8: "8"; _: "9";
    }
}

fn intToStr(n Int) String {
    if (n < 10) { digitChar(n) }
    else { intToStr(n // 10) $ digitChar(n % 10) }
}

fn fizzbuzz(n Int) String {
    if (n % 15 == 0) { "FizzBuzz" }
    else if (n % 3 == 0) { "Fizz" }
    else if (n % 5 == 0) { "Buzz" }
    else { n intToStr }
}

-- Auto-map fizzbuzz over the range
(1..20) toArray fizzbuzz println;
