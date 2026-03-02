-- Harshad or Niven Series (Rosetta Code)
-- A Harshad number is divisible by the sum of its digits.
-- Recursive digit sum, filter over ranges.

fn digitSum(n Int) Int {
    if (n < 10) { n }
    else { n % 10 + digitSum(n // 10) }
}

fn isHarshad(n Int) Bool = n % (n digitSum) == 0;

-- First 20 Harshad numbers: filter the naturals
(1..1000) toArray filter(fn(n Int) Bool { n isHarshad }) take(20) println;

-- First Harshad number > 1000
(1001..2000) toArray filter(fn(n Int) Bool { n isHarshad }) take(1) println;
