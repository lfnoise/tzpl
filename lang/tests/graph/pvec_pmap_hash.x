-- PVec/PMap hashing is structural (was pointer identity, which broke the
-- hash law: equal persistent collections hashed differently, so they were
-- unreliable as Map/Set keys).

let a = #[1, 2, 3];
let b = #[1, 2, 3];
(a == b) println;
(a hash == b hash) println;
(a hash != #[1, 2, 4] hash) println;

-- separately built pmaps with different insertion orders
let pm1 = #["x": 1, "y": 2, "z": 3];
var m2 = #["z": 3];
let pm2 = m2 put("y", 2) put("x", 1);
(pm1 == pm2) println;
(pm1 hash == pm2 hash) println;

-- persistent collections as hash keys now round-trip via equal copies
var mm = [a: "vec"];
(mm[b] unwrap) println;
var ss = Set(pm1);
(ss contains(pm2)) println;

-- nested
let nested1 = #[#[1, 2], #[3]];
let nested2 = #[#[1, 2], #[3]];
(nested1 hash == nested2 hash) println;

-- a cycle threading through a PVec (pvec -> enum -> array -> enum -> pvec)
-- terminates in both == and hash
enum T {
    pnode #[T],
    anode [T],
    leaf Int,
}
var arr1 = [T.leaf(1)];
let pv1 = #[T](T.anode(arr1));
arr1 push!(T.pnode(pv1));
var arr2 = [T.leaf(1)];
let pv2 = #[T](T.anode(arr2));
arr2 push!(T.pnode(pv2));
(arr1 == arr2) println;
(arr1 hash == arr2 hash) println;
(arr1 hash == arr1 hash) println;
