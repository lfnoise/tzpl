-- @fails
-- panic(msg): print "Error: <msg>", halt execution, and exit nonzero.
-- Statements after the panic must not run.

"before panic" println;
panic("something went wrong");
"after panic -- must not print" println;
