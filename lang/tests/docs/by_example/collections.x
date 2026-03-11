-- Collection Types from Language X by Example

-- Array literals
let nums = [1, 2, 3, 4, 5];
nums println;

-- Tuple literals (heterogeneous, fixed-length)
let point = (3, 4);
let mixed = (1, 2.5, "hello");
point println;
mixed println;

-- 1-element tuple (trailing comma distinguishes from parenthesized expression)
let single = (42,);
single println;
single.0 println;

-- Empty tuple (unit value)
let u = ();
u println;

-- Trailing commas are allowed in all tuples
let t3 = (1, 2, 3,);
t3 println;

-- Map literals
let ages = ["Alice": 30, "Bob": 25];
ages println;

-- Set construction
let unique = Set(1, 2, 3);
unique println;

-- Access by index
let first = nums[0];
let x = point.0;
first println;
x println;

-- Array indexing is cyclic (wraps around)
let a = [10, 20, 30];
a[0] println;
a[3] println;
a[-1] println;
a[-2] println;
