// Project-root discovery shared by both GUI backends and the startup path.
//
// A project is a directory containing a `tzpl-config` file (engine settings,
// key = value). The legacy marker name `config` is still honored, but only
// as a fallback: `config` is too generic a filename to be authoritative
// (nginx checkouts, dotfile trees, ...), so a `tzpl-config` anywhere up the
// ancestor chain beats a nearer `config`. The marker is the config file
// alone -- a modules/ directory is project *content*, not a marker -- so a
// distribution folder's modules/ or the source tree's lang/modules/ never
// make their parent look like a project (which would, among other things,
// point synthdef dylib output at a possibly read-only volume).

#pragma once

#include <string>

namespace tzplapp {

// Walk up from `filePath` (a file the user opened; need not be canonical)
// to the nearest ancestor directory containing `tzpl-config`; if none is
// found, the nearest ancestor containing legacy `config`. Returns the
// project root directory, or "" if neither is found.
std::string findProjectRoot(std::string const& filePath);

// The config file inside a project root: <root>/tzpl-config if it exists,
// else <root>/config (legacy), else "".
std::string projectConfigFile(std::string const& projectRoot);

}  // namespace tzplapp
