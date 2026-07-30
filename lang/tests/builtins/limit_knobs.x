-- Advanced per-VM limit knobs (VMConfig): graph-traversal limits are live
-- values adjustable at runtime. Getters return the current value; setters
-- return the PREVIOUS value (handy for scoped save/restore) and clamp to 1.

-- Defaults.
getGraphMaxDepth() println;
getLazyForceLimit() println;
getPrintMaxDepth() println;
getListPrintLimit() println;

-- Setters return the old value.
setGraphMaxDepth(500) println;
getGraphMaxDepth() println;
setGraphMaxDepth(10000) println;

setLazyForceLimit(123) println;
getLazyForceLimit() println;
setLazyForceLimit(10000) println;

-- Values clamp to >= 1.
setPrintMaxDepth(0);
getPrintMaxDepth() println;

-- Print depth elides deep nesting with "..." instead of erroring.
setPrintMaxDepth(2);
[[[[1]]]] println;
setPrintMaxDepth(200);
[[[[1]]]] println;
