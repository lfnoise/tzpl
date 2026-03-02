-- String Formatting (fmt) from Language X by Example

-- %^ is a positional placeholder (filled left to right)
"%^ + %^ = %^" fmt(1, 2, 3) println;

-- %0 through %9 select by index (zero-based)
"%0 and %1 and %0" fmt("x", "y") println;

-- %% produces a literal percent sign
"100%% done" fmt() println;

-- Works with any type
"value: %^" fmt(42) println;
"pi = %^" fmt(3.14) println;
"list: %^" fmt(1::2::3::nil) println;

-- Zero args is fine when no placeholders are used
"no placeholders" fmt() println;
