-- QA: BUG - reverse doesn't work on strings
-- The reverse function is only overloaded for arrays and lists

-- This works:
[1, 2, 3] reverse println;

-- This now works:
"hello" reverse println;

-- Workaround (no longer needed): split, reverse array, join
let s = "hello";
let chars = split(s, "");
chars reverse println;

-- Also, toUpper/toLower work on strings
"hello" toUpper println;
"HELLO" toLower println;
