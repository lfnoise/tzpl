-- Auto-mapping in array and tuple literals

-- Array literal with zip @
[[1, 2, 3]@, 4, 5] println;

-- Array literal with two zipped arrays
[[1, 2, 3]@, [10, 20, 30]@] println;

-- Array literal with different lengths (min)
[[1, 2, 3, 4, 5]@, [10, 20, 30]@] println;

-- Array literal with cartesian @1/@2
[[1, 2, 3]@1, [4, 5, 6]@2] println;

-- Tuple literal with zip @
([1, 2, 3]@, "hello") println;

-- Tuple literal with two zipped arrays
([1, 2, 3]@, [10, 20, 30]@) println;

-- Tuple literal with cartesian @1/@2
([1, 2, 3]@1, [4, 5, 6]@2) println;

-- Array literal with 3-level cartesian @1/@2/@3
[[1, 2]@1, [3, 4]@2, [5, 6]@3] println;

-- Tuple literal with 3-level cartesian @1/@2/@3
([1, 2]@1, [3, 4]@2, [5, 6]@3) println;

-- Array literal with numeric promotion (Int elements alongside Float array)
[[1.0, 2.0, 3.0]@, 0] println;
