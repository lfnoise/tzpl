-- QA: Sort and comparison edge cases

-- Sort ascending (default)
[3, 1, 4, 1, 5, 9, 2, 6] sort println;

-- Sort with custom comparator
[3, 1, 4, 1, 5] sort(fn(a Int, b Int) Bool { a > b }) println;  -- descending

-- Sort strings
["banana", "apple", "cherry"] sort println;

-- Sort single element
[42] sort println;

-- Sort empty
[Int]() sort println;

-- Sort already sorted
[1, 2, 3, 4, 5] sort println;

-- Sort reverse sorted
[5, 4, 3, 2, 1] sort println;

-- Sort with duplicates
[3, 3, 1, 1, 2, 2] sort println;

-- cmp function
println(cmp(1, 2));      -- -1
println(cmp(2, 2));      -- 0
println(cmp(3, 2));      -- 1
println(cmp("a", "b"));  -- -1
println(cmp("b", "a"));  -- 1
println(cmp("a", "a"));  -- 0

-- Sort structs with custom comparator
struct Student { name String, grade Int }
let students = [
    Student{"Charlie", 85},
    Student{"Alice", 92},
    Student{"Bob", 78}
];
let by_grade = students sort(fn(a Student, b Student) Bool { a.grade < b.grade });
by_grade.name println;     -- [Bob, Charlie, Alice]
by_grade.grade println;    -- [78, 85, 92]

-- Sort by name
let by_name = students sort(fn(a Student, b Student) Bool { a.name < b.name });
by_name.name println;     -- [Alice, Bob, Charlie]

-- grade with custom comparator
grade(["c", "a", "b"], fn(a String, b String) Bool { a < b }) println;
