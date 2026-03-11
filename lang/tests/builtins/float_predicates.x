-- Float predicates

let nan = 0.0 / 0.0;
let inf = 1.0 / 0.0;

-- isNan
nan isNan println;
1.0 isNan println;
inf isNan println;
0.0 isNan println;

-- isInf
inf isInf println;
-inf isInf println;
1.0 isInf println;
nan isInf println;
0.0 isInf println;
