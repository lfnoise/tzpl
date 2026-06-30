-- Typed array/persistent-vector constructors: the element type sits in the
-- bracket slot and the elements follow in parens -- [Type](e1, e2, ...) and the
-- persistent form #[Type](...). The element type pins inference (numeric
-- literals promote, empties get a concrete type) and, for a `some C` element
-- type, each element is packed into the existential.
constraint CanSpeak<T> = requires { speak(T) String };

struct Dog();
struct Cat();
struct Cow();
fn speak(x Dog) = "arf";
fn speak(x Cat) = "meow";
fn speak(x Cow) = "moo";

-- Homogeneous, element type pins promotion (Int literals -> Float).
[Float](1, 2, 3) println;            -- [1.0, 2.0, 3.0]
#[Float](1, 2, 3) println;           -- #[1.0, 2.0, 3.0]

-- Nested element type via the [T] shorthand.
[[Int]]([1, 2], [3]) println;        -- [[1, 2], [3]]

-- Empty typed array gets its element type from the constructor, not context.
[Int]() length println;              -- 0

-- Heterogeneous existential literals: pack each concrete value into `some C`.
let a = [some CanSpeak](Cow(), Dog(), Cat());
a speak println;                     -- [moo, arf, meow]

-- Persistent (#) existential form.
let p = #[some CanSpeak](Dog(), Cow());
p speak println;                     -- #[arf, moo]

-- Works inline in expression position (no binding annotation needed).
([some CanSpeak](Dog(), Cow()) speak) println;   -- [arf, moo]

-- Empty existential array.
[some CanSpeak]() length println;    -- 0
