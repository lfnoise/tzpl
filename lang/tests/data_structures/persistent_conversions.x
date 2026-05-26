-- Conversions between mutable and persistent collections

-- Array <-> persistent vector
let arr = [5, 6, 7];
let pv = arr toPersistentVector;
pv println;
pv length println;

let back = pv toArray;
back println;

-- round-trip preserves contents
(arr toPersistentVector toArray == arr) println;

-- Map <-> persistent map
let mp = ['x: 1, 'y: 2] toPersistentMap;
mp length println;
mp['x] unwrap println;

let mback = mp toMap;
mback length println;
mback['x] unwrap println;

-- a converted persistent vector is independent of a later mutation of the source
var src = [1, 2, 3];
let frozen = src toPersistentVector;
src push!(4);
src length println;        -- 4
frozen length println;     -- 3 (unaffected)
