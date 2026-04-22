-- Wildcard-importing this bridge should register both the local
-- twice_squared function AND the re-exported `math_utils` alias.
import math_reexport_whole.*;

3 twice_squared println;
println(math_utils.square(5));
println(math_utils.cube(2));
