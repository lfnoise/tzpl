// Discovery of module search paths shared by every entry point (CLI, apps,
// tests). The ModuleCompiler itself just consumes an ordered list of
// directories; these helpers let each executable compose that list the same
// way:
//
//     -I paths -> project modules/ -> envModulePaths() -> defaultModulePaths()
//
// The stdlib comes last so a project (or $TZPL_PATH entry) can shadow a
// stdlib module by providing a file with the same name.

#pragma once

#include <string>
#include <vector>

namespace ts {

// Directories listed in $TZPL_PATH (colon-separated), in order.
std::vector<std::string> envModulePaths();

// Locate the installed standard-library module directories. Candidates are
// checked in order and every existing directory is returned (canonicalized,
// deduplicated):
//
//   1. $TZPL_HOME/modules -- explicit override (CI, side-by-side versions)
//   2. the nearest ancestor of the running executable that contains a
//      modules/ directory -- finds a distribution folder from anywhere
//      inside it, e.g. Tzopilotl/bin/tzpl or
//      Tzopilotl/Tzopilotl.app/Contents/MacOS/Tzopilotl next to
//      Tzopilotl/modules/
//   3. `fallbacks` -- compiled-in source-tree paths, so dev builds (whose
//      build directory has no modules/ ancestor) work with no setup
std::vector<std::string> defaultModulePaths(
    std::vector<std::string> const& fallbacks = {});

}  // namespace ts
