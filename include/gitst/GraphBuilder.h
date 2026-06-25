#pragma once

#include "gitst/Models.h"

namespace gitst {

// Turns raw, unordered repository data into a laid-out graph:
//   1. Topologically orders commits (children above parents, newest first).
//   2. Assigns each commit to a lane (column), keeping first-parent chains
//      vertical the way real git history viewers do.
//   3. Colors lanes, reserving a fixed color for the default branch mainline.
//   4. Emits node-to-parent edges with resolved geometry.
//
// The provided RepoData is consumed (its commits are moved into the result).
RepoGraph buildGraph(const RepoRef& ref, RepoData&& data);

} // namespace gitst
