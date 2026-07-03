constraint CanSpeak<T> = requires { speak(T) String }; 

struct Dog();
struct Cat();
struct Cow();
struct Crow();

fn speak(x Dog) = "arf";
fn speak(x Cat) = "meow"; 
fn speak(x Cow) = "moo";
fn speak(x Crow) = "caw";

let a = [some CanSpeak](Cow(), Crow(), Dog(), Cat());
let b = [some CanSpeak](Dog(), Crow());
let c = [a, b];

c @@ speak println;
c @ speak println;
c speak println;

[a @1, b @2] speak println;
[a @2, b @1] speak println;

[1, 2, 3, 4] picks collect(10) println;

a picks collect(10) println;
a picks speak collect(10) println;
 


