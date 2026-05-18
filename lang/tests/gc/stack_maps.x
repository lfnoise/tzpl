-- Phase 2 of tracing-GC project: confirm stack maps are emitted at every
-- op_safepoint and that the live-reference register set is what we expect.
-- The disassembler annotates each SAFEPOINT line with "refs: [...]".
--
-- Scalar params/locals (Int, Float, Bool) must produce empty ref sets.
-- Obj-typed locals (String, Array, etc.) must appear in the ref set at
-- safepoints where they are in scope.

-- All-scalar function: function-entry safepoint, no live refs.
fn scalar_only(x Int, y Int) Int {
    x + y
}

-- Obj-typed parameter: function-entry safepoint should report the
-- parameter register as a live ref.
fn obj_param(s String) Int {
    s length
}

-- Loop with an Obj-typed local: mid-loop safepoint should include that local.
fn loop_with_obj(n Int) Int {
    var s = "x";
    var i = 0;
    while (i < n) {
        s = s $ "y";
        i = i + 1;
    }
    s length
}

disassemble(scalar_only);
disassemble(obj_param);
disassemble(loop_with_obj);
