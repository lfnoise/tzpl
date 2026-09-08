// Project-root discovery shared by both GUI backends and the startup path.
//
// A project is a directory containing a `tzpl-config` file (engine settings,
// key = value). The distinctive name is deliberate: a generic marker like
// `config` would false-positive on unrelated trees (nginx checkouts, dotfile
// repos, ...). The marker is the config file alone -- a modules/ directory
// is project *content*, not a marker -- so a distribution folder's modules/
// or the source tree's lang/modules/ never make their parent look like a
// project (which would, among other things, point synthdef dylib output at
// a possibly read-only volume).

#pragma once

#include <string>
#include <vector>

namespace tzplapp {

// Walk up from `filePath` (a file the user opened; need not be canonical)
// to the nearest ancestor directory containing `tzpl-config`. Returns the
// project root directory, or "" if none is found.
std::string findProjectRoot(std::string const& filePath);

// The config file inside a project root: <root>/tzpl-config if it exists,
// else "".
std::string projectConfigFile(std::string const& projectRoot);

// Qualifiers that tell apart same-named folders in a list of roots (the
// sidebar shows a root by its leaf name, so two modules/ folders are
// indistinguishable without one). Returns one entry per input path: "" for
// a root whose leaf name is unique in the list, otherwise the shortest run
// of trailing parent directories that differs among the same-named roots,
// written "\u2026/tzpl_1/tzpl/lang". All roots of one name get the same
// depth so they line up. When the run reaches the filesystem root the
// whole parent path is shown instead (with `home` abbreviated to "~").
// Identical paths get identical qualifiers; dedupe belongs to the caller.
std::vector<std::string> rootQualifiers(std::vector<std::string> const& paths,
                                        std::string const& home = {});

}  // namespace tzplapp
