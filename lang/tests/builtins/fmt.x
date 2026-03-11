-- fmt builtin function

-- Positional %^
"%^ + %^ = %^" fmt(1, 2, 3) println;

-- Indexed %0-%9
"%0 and %2 and %1" fmt("a", "b", "c") println;

-- Escape %%
"100%% done" fmt() println;

-- Bool
"%^ is %^" fmt(true, false) println;

-- Float
"%^ and %^" fmt(3.14, -0.5) println;

-- Fraction
"half = %^" fmt(1/2) println;

-- Tuple
let t = (7, 8, 9);
"tuple: %^" fmt(t) println;

-- List
"list: %^" fmt(1::2::3::nil) println;

-- Array
"array: %^" fmt([10, 20, 30]) println;

-- String value in tuple
"hello %^!" fmt("world") println;

-- Mixed types
"%^ %^ %^ %^ %^ %^" fmt(123, 4.56, (7,8,9), 4/5, 1::2::3::nil, [1,2,3]) println;

-- Indexed access repeats
"%0 %0 %0" fmt("echo") println;

-- Mixed positional and indexed
"%^ then %0 again" fmt("first", "second") println;

-- No placeholders
"no placeholders" fmt() println;

-- Pipeline style
let s = "%^ items" fmt(42);
s println;

-- Struct
struct Point { x Float, y Float }
"point: %^" fmt(Point { x: 1.0, y: 2.0 }) println;

-- Enum
enum Color { red, green, blue }
"color: %^" fmt(Color.green) println;

-- Map
"map: %^" fmt(['a:1, 'b:2]) println;

-- Trailing percent
"trail%" fmt() println;

-- Adjacent escapes
"%%%%" fmt() println;

-- Percent then positional
"%%=%^" fmt(42) println;
