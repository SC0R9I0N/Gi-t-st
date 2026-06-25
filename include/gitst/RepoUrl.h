#pragma once

#include <string>

#include "gitst/Models.h"

namespace gitst {

// Parses a user supplied repository reference into a fully resolved RepoRef.
//
// Accepts a wide range of inputs:
//   https://github.com/owner/repo            (with or without .git / trailing /)
//   git@github.com:owner/repo.git
//   https://gitlab.com/group/subgroup/repo   (GitLab subgroups supported)
//   github.com/owner/repo
//   owner/repo                               (assumed GitHub)
//
// Self-hosted GitHub Enterprise / GitLab hosts are detected heuristically from
// the hostname. On success `ok` is true; otherwise `error` explains why.
struct ParsedUrl {
    bool ok = false;
    RepoRef ref;
    std::string error;
};

ParsedUrl parseRepoUrl(const std::string& input);

// Percent-encodes a string for safe use inside a URL path segment.
std::string urlEncode(const std::string& s);

} // namespace gitst
