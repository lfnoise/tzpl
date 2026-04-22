-- math_bridge uses `import math_utils.*` (not `export import`),
-- so math_utils' `square` must NOT leak through to this file.
-- math_bridge's own `quintuple` is exported normally.
import math_bridge.*;

4 quintuple println;
5 square println;
