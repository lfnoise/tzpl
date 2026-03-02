-- String Formatting (fmt) from Builtin_Functions.html

-- Positional placeholders
"%^ + %^ = %^" fmt(1, 2, 3) println;

-- Indexed placeholders
"%0 and %2 and %1" fmt("a", "b", "c") println;

-- Repeated index
"%0 %0 %0" fmt("echo") println;

-- Escape percent
"100%% done" fmt() println;
"%%=%^" fmt(42) println;

-- Works with any type
"half = %^" fmt(1/2) println;
"array: %^" fmt([10, 20, 30]) println;
"list: %^" fmt(1::2::3::nil) println;

-- Pipeline style
"hello %^!" fmt("world") println;
"%^ items" fmt(42) println;

-- Zero args
"no placeholders" fmt() println;
