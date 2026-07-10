// Project-root discovery shared by both GUI backends and the startup path.
//
// A project is a directory containing a `config` file (the same file
// -P/--project reads: engine settings, key = value). The marker is the
// config file alone -- a modules/ directory is project *content*, not a
// marker -- so a distribution folder's modules/ or the source tree's
// lang/modules/ never make their parent look like a project (which would,
// among other things, point synthdef dylib output at a possibly read-only
// volume).

#pragma once

#include <string>

namespace tzplapp {

// Walk up from `filePath` (a file the user opened; need not be canonical)
// to the nearest ancestor directory containing a `config` file. Returns the
// project root directory, or "" if none is found.
std::string findProjectRoot(std::string const& filePath);

}  // namespace tzplapp
