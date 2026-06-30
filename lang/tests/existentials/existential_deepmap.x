-- Deep auto-mapping of a constraint method over nested collections of
-- existentials. Implicit auto-map, explicit `@` (maps at least one level, the
-- rest implicitly), and explicit full-depth `@@`/`@@@` all dispatch through the
-- witness at every level and produce the same nested result. Works across array,
-- list, and persistent-vector nesting, including mixed nesting.
constraint CanSpeak<T> = requires { speak(T) String };

struct Dog(); struct Cat(); struct Cow(); struct Crow();
fn speak(x Dog) = "arf";
fn speak(x Cat) = "meow";
fn speak(x Cow) = "moo";
fn speak(x Crow) = "caw";

let a [some CanSpeak] = [Cow(), Crow(), Dog(), Cat()];
let b [some CanSpeak] = [Dog(), Dog()];
let c = [a, b];                 -- [[some CanSpeak]]

c speak println;                -- implicit, depth 2
(c @ speak) println;            -- explicit depth 1 + implicit inner
(c @@ speak) println;           -- explicit full depth

-- Triple array nesting.
let t [[[some CanSpeak]]] = [[a, b], [a]];
t speak println;
(t @@@ speak) println;

-- List of arrays of existentials (mixed nesting).
let la List<[some CanSpeak]> = List(a, b);
la speak println;
(la @@ speak) println;

-- Persistent vector of existentials (single level).
let pv #[some CanSpeak] = #[Dog(), Cat()];
pv speak println;
(pv @ speak) println;
