-- put! (map), remove! (map and set): mutate in place, return same container.

-- put! inserts and updates the receiver, not a copy.
var m = ["x": 1, "y": 2];
var alias = m;
m put!("z", 3);
alias length println;          -- 3 (alias sees the insert)
m get("z", 0) println;         -- 3

m put!("x", 99);               -- update existing key
m get("x", 0) println;         -- 99
m length println;              -- 3 (update, not insert)

-- put! returns the same map for chaining.
m put!("a", 10) put!("b", 20);
m length println;              -- 5

-- remove! on a map mutates the receiver.
m remove!("a");
alias contains("a") println;   -- false (alias sees the removal)
m length println;              -- 4

-- removing an absent key is a no-op.
m remove!("nope");
m length println;              -- 4

-- remove! on a set mutates the receiver.
var s = Set(1, 2, 3);
var st = s;
s remove!(2);
st length println;             -- 2 (alias sees the removal)
s contains(2) println;         -- false

-- removing an absent element is a no-op.
s remove!(99);
s length println;              -- 2

-- copy stays independent of later put!/remove!.
let snap = m copy;
m put!("c", 30);
m remove!("b");
snap length println;           -- 4 (unaffected)
m length println;              -- 4
