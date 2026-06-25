#pragma once

#include <memory>
#include <string>

#include "gitst/HttpClient.h"
#include "gitst/Models.h"

namespace gitst {

// Abstract interface for a forge backend. Implementations know how to talk to a
// specific REST API and return provider-neutral RepoData.
class GitProvider {
public:
    virtual ~GitProvider() = default;

    // Fetches branches + commit history for `ref`. On failure returns false and
    // fills `error`; partial results may still be present in `out` (and `out`'s
    // truncated flag / notes describe any limits that were hit).
    virtual bool fetch(const RepoRef& ref,
                       const FetchOptions& opts,
                       RepoData& out,
                       std::string& error) = 0;
};

// Creates the appropriate provider for a parsed reference. Returns nullptr for
// Provider::Unknown.
std::unique_ptr<GitProvider> makeProvider(Provider provider, HttpClient& client);

} // namespace gitst
