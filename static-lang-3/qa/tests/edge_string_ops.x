-- QA: String operations

-- Concatenation
println("hello" $ " " $ "world");

-- String length
println("hello" length);      -- 5
println("" length);           -- 0

-- String comparison
println("abc" < "abd");       -- true
println("abc" == "abc");      -- true
println("abc" != "def");      -- true
println("a" < "b");           -- true
println("z" > "a");           -- true

-- String contains
println("hello world" contains("hello"));  -- true
println("hello world" contains("xyz"));    -- false
println("hello" contains(""));             -- true

-- toUpper / toLower
println("hello" toUpper);     -- HELLO
println("WORLD" toLower);     -- world
println("" toUpper);          -- (empty)
println("123abc" toUpper);    -- 123ABC

-- split
println(split("a,b,c", ","));        -- [a, b, c]
println(split("hello", ""));         -- [h, e, l, l, o]
println(split("no-delim", "X"));     -- [no-delim]

-- split into chars (no 'chars' builtin, use split)
println(split("hello", ""));         -- [h, e, l, l, o]

-- reverse (now works after bug fix)
println("hello" reverse);            -- olleh
println("" reverse);                 -- (empty)
println("a" reverse);                -- a

-- String indexing (byte access)
println("hello"[0]);         -- 104 (h)
println("hello"[4]);         -- 111 (o)

-- String from format
println("value: %^" fmt(42));
println("%^ and %^" fmt("a", "b"));

-- String repetition via auto-map
-- Build string by concatenation
var s = "";
for (i : (1..5)) { s = s $ toString(i); }
s println;   -- 12345

-- substring
println(substring("hello world", 0, 5));
println(substring("hello world", 6, 5));
