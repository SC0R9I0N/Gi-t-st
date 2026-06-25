#include "gitst/Fetcher.h"

#include <cstdlib>

#include "gitst/DemoData.h"
#include "gitst/GraphBuilder.h"
#include "gitst/HttpClient.h"
#include "gitst/Provider.h"
#include "gitst/RepoUrl.h"

namespace gitst {
namespace {

std::string envOr(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

} // namespace

FetchResult fetchGraph(const std::string& url, const FetchOptions& optsIn) {
    FetchResult result;

    ParsedUrl parsed = parseRepoUrl(url);
    if (!parsed.ok) {
        result.error = parsed.error;
        return result;
    }

    HttpClient client;
    client.setUserAgent("Gi-t-st/1.0 (+https://github.com)");
    auto provider = makeProvider(parsed.ref.provider, client);
    if (!provider) {
        result.error = "unsupported provider for host '" + parsed.ref.host + "'";
        return result;
    }

    FetchOptions opts = optsIn;
    if (opts.token.empty()) {
        opts.token = (parsed.ref.provider == Provider::GitLab) ? envOr("GITLAB_TOKEN")
                                                               : envOr("GITHUB_TOKEN");
    }

    RepoData data;
    std::string error;
    if (!provider->fetch(parsed.ref, opts, data, error)) {
        result.error = error;
        return result;
    }

    result.graph = buildGraph(parsed.ref, std::move(data));
    if (result.graph.commits.empty()) {
        result.error = "no commits found for this repository";
        return result;
    }
    result.ok = true;
    return result;
}

FetchResult buildDemoGraph() {
    FetchResult result;
    RepoRef ref;
    ref.provider = Provider::GitHub;
    ref.owner = "gitst";
    ref.repo = "demo";
    ref.webBase = "https://example.com/demo/repo";
    result.graph = buildGraph(ref, makeDemoRepo());
    result.ok = true;
    return result;
}

} // namespace gitst
