-- Spiral Matrix (Rosetta Code)
-- Compute each cell's value from its (row, col) position using the ring formula.
-- Auto-maps spiralVal over columns, then auto-maps spiralRow over rows.

fn spiralVal(n Int, r Int, c Int) Int {
    let layer = min(min(r, c), min(n - 1 - r, n - 1 - c));
    let outer = 4 * layer * (n - layer);
    let innerN = n - 2 * layer;
    if (r == layer) {
        outer + (c - layer) + 1
    } else if (c == n - 1 - layer) {
        outer + (innerN - 1) + (r - layer) + 1
    } else if (r == n - 1 - layer) {
        outer + 2 * (innerN - 1) + (n - 1 - layer - c) + 1
    } else {
        outer + 3 * (innerN - 1) + (n - 1 - layer - r) + 1
    }
}

-- Auto-map: spiralVal(4, row, col) where col auto-maps over 0..3,
-- then row auto-maps over 0..3
fn spiralRow(n Int, r Int) [Int] =
    spiralVal(n, r, (0..(n - 1)) toArray);

-- Auto-map spiralRow over row indices
spiralRow(4, (0..3) toArray) println;
