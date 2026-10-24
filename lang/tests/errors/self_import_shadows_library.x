-- A file named after a library module that imports that module resolves
-- the import to itself; the diagnostic names the shadowed library module
-- (issue #15: a notebook saved as live.x doing `import live.*`).
import _lib.dsp_math;

println(1);
