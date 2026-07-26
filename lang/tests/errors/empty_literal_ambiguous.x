-- An empty collection literal takes its type from the parameter it is passed
-- to, but only when the overloads that fit the other arguments agree on what
-- that parameter is. Here they do not, so the literal stays untyped.

fn amb(m [Int:Int]) String { "a" }
fn amb(m [String:String]) String { "b" }

println(amb([:]));
