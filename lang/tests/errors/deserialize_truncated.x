-- A truncated (but well-prefixed) buffer is a clean runtime error.
let good = serialize([1, 2, 3]);
var trunc = bytes();
var i = 0;
while (i < good byteLength - 5) {
    trunc putU8!(good u8At(i));
    i = i + 1;
}
deserialize<[Int]>(trunc) println;
