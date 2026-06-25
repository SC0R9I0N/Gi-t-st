#pragma once

#include <string>

#include "gitst/Models.h"

namespace gitst {

// Result of a (synchronous) fetch + layout pass.
struct FetchResult {
    bool ok = false;
    std::string error;   // human-readable failure reason when !ok
    RepoGraph graph;
};

// Parses `url`, fetches the repository from the appropriate provider, and lays
// out the graph. Reads GITHUB_TOKEN / GITLAB_TOKEN from the environment to
// raise rate limits. Safe to call from a background thread.
FetchResult fetchGraph(const std::string& url, const FetchOptions& opts = {});

// Builds the offline demo graph (no network access required).
FetchResult buildDemoGraph();

} // namespace gitst
