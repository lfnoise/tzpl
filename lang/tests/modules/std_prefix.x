-- Test that std. prefix accesses built-in functions.
-- Also test that module.func() works for wildcard imports.
import shadow_builtins.*;

-- Built-in versions via std. prefix
std.abs(-5) println;
std.max(3, 7) println;

-- Module versions via qualified access
shadow_builtins.abs(-5) println;
shadow_builtins.max(3, 7) println;
