-- Variables and Constants from Tzopilotl by Example

-- Immutable bindings (let)
let x = 42;
let pi = 3.14159;
let name = "Alice";
x println;
pi println;
name println;

-- Optional type annotation
let typed_var Int = 100;
typed_var println;

-- Mutable variables (var)
var counter = 0;
var message = "hello";
counter println;
message println;

-- Can be reassigned
counter = counter + 1;
message = "goodbye";
counter println;
message println;

-- Constants
const MAX_SIZE = 1000;
const TAU = 6.28318;
MAX_SIZE println;
TAU println;
