struct Point { x Int, y Int }
struct Named { id Int, name String }

enum Shape {
    circle Float,
    rect (Int, Int),
    nothing
}

-- Inline-tuple dynvar at module scope (init only, no scope push)
var `gtup = (1, 2, 3);
`gtup println;
`gtup = (10, 20, 30);
`gtup println;

-- Struct dynvar
var `gpoint = Point{0, 0};
`gpoint println;
`gpoint = Point{42, 99};
`gpoint println;

-- Struct with String field (ARC walking on overwrite)
var `gnamed = Named{0, "init"};
`gnamed println;
`gnamed = Named{1, "again"};
`gnamed = Named{2, "third"};
`gnamed println;

-- Enum dynvar
var `gshape = Shape.circle(1.0);
`gshape println;
`gshape = Shape.rect((3, 4));
`gshape println;
`gshape = Shape.nothing;
`gshape println;

-- In-function rebind (exercises op_dynscope_push_inline + restore)
fn inner() Void {
    `gpoint println;          -- before rebind
    var `gpoint = Point{77, 88};  -- save current, set new
    `gpoint println;          -- new value
    `gpoint = Point{100, 200};
    `gpoint println;          -- modified inside the scope
}

inner();
`gpoint println;             -- should be restored to outer Point{42, 99}

-- Nested rebinds
fn outer_fn() Void {
    var `gpoint = Point{1, 1};
    `gpoint println;
    inner();                  -- inner pushes Point{77,88} on top of {1,1}
    `gpoint println;          -- should be back to Point{1, 1}
}

outer_fn();
`gpoint println;             -- back to Point{42, 99}

-- Same with String fields (ARC under save/restore)
fn rebind_named() Void {
    var `gnamed = Named{10, "inner"};
    `gnamed println;
    `gnamed = Named{11, "inner2"};
    `gnamed println;
}

rebind_named();
`gnamed println;            -- restored to Named{2, "third"}
