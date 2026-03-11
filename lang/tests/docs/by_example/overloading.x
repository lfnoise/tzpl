-- Function Overloading from Tzopilotl by Example

fn describe(x Int) String = "an integer";
fn describe(x Float) String = "a float";
fn describe(x String) String = "a string";

42 describe println;
3.14 describe println;
"hi" describe println;
