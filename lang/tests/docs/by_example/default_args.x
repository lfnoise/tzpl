-- Default Arguments from Tzopilotl by Example

-- Basic default value
fn greet(name String, greeting String = "Hello") String = greeting $ " " $ name;
greet("World", "Hi") println;
greet("World") println;

-- Multiple defaults
fn range_info(start Int = 0, stop Int = 10, step Int = 1) String {
    "%^ to %^ by %^" fmt(start, stop, step)
}
range_info(1, 5, 2) println;
range_info(1, 5) println;
range_info(1) println;
range_info() println;

-- Default referencing a preceding parameter
fn make_range(lo Int, hi Int = lo + 10) String = "%^ to %^" fmt(lo, hi);
make_range(5, 20) println;
make_range(5) println;
