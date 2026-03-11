-- Flatten a List (Rosetta Code)
-- Use join (flatten one level) and flatten (all levels).

[[1, 2, 3], [4, 5], [6, 7, 8, 9]] join println;

[[[1, 2], [3]], [[4, 5, 6]]] join join println;

[[10, 20], [30, 40], [50]] flatten println;
