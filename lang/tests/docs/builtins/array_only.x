-- Array-only Functions from Builtin_Functions.html

-- reverse
println([1, 2, 3, 4, 5] reverse);
println([1, 2, 3] reverse);

-- push
println([1, 2, 3] push(4));

-- pop
println([1, 2, 3, 4] pop);
println([1, 2, 3] pop);

-- sort
println([3, 1, 4, 1, 5, 9, 2, 6] sort);
println([5, 3, 1, 4, 2] sort);

-- clear!
var ca = [1, 2, 3];
ca clear!;
println(ca);
println(ca length);

-- append!
var aa = [1, 2];
aa append!([3, 4]);
println(aa);
aa append!(List(5, 6));
println(aa);
