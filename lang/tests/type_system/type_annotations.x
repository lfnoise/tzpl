-- Type annotations

-- Basic annotations
let n Int = 42;
n println;

let f Float = 2.5;
f println;

let s String = "hello";
s println;

let b Bool = true;
b println;

-- Array type annotation
let arr [Int] = [1, 2, 3];
arr println;

-- List type annotation
let lst List<Int> = List(1, 2, 3);
lst println;

-- Tuple type annotation
let p (Int, Int) = (1, 2);
p println;

-- Empty list with type
let empty List<Int> = nil;
empty println;
