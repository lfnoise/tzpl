-- repl_init_helper.x
-- Helper module for run_repl_module_init_test.sh: module-level state that
-- only exists after the module's init block has run.
let table = [10, 20, 30];
fn readTable() Int = table length;
