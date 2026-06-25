#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace gitst {

// Which forge a repository lives on. Used to pick the correct REST API shape.
enum class Provider {
    Unknown,
    GitHub,
    GitLab
};

const char* providerName(Provider p);

// A fully-resolved reference to a remote repository, produced by parsing a
// user supplied URL. Contains everything the providers need to build requests.
struct RepoRef {
    Provider provider = Provider::Unknown;
    std::string host;        // e.g. "github.com"
    std::string owner;       // e.g. "torvalds"
    std::string repo;        // e.g. "linux"
    std::string projectPath; // full path, may include subgroups (GitLab)
    std::string apiBase;     // e.g. "https://api.github.com"
    std::string webBase;     // e.g. "https://github.com/torvalds/linux"
};

// A single commit node in the graph. The first block of fields is raw VCS data
// fetched from the provider; the second block is layout data computed locally.
struct Commit {
    // --- Raw data -----------------------------------------------------------
    std::string sha;
    std::string shortSha;
    std::string summary;        // first line of the message
    std::string message;        // full message
    std::string authorName;
    std::string authorEmail;
    std::string authorDate;     // ISO-8601 string as returned by the provider
    std::int64_t authorTs = 0;  // author date as a unix timestamp (parsed)
    std::string committerName;
    std::string committerDate;
    std::vector<std::string> parents;
    std::string url;            // web URL to the commit

    // --- Computed layout ----------------------------------------------------
    int row = -1;               // vertical position (0 = newest, at the top)
    int lane = -1;              // horizontal column the commit dot sits in
    std::string color;          // hex color for this commit's node
    bool isMerge = false;       // more than one parent
    bool onMainline = false;    // on the default branch's first-parent chain
    std::vector<std::string> branchHeads; // branch names whose tip is this commit
};

// A drawable connection between a child commit and one of its parents.
struct Edge {
    std::string from;   // child sha
    std::string to;     // parent sha
    int fromRow = -1;
    int fromLane = -1;
    int toRow = -1;
    int toLane = -1;
    std::string color;
    bool merge = false; // true when this is a non-first-parent (merge) edge
};

// A named branch and the commit it points at.
struct Branch {
    std::string name;
    std::string tipSha;
    bool isDefault = false;
    std::string color;
};

// Raw repository data as returned by a provider, before layout is computed.
struct RepoData {
    std::string defaultBranch;
    std::string webUrl;
    std::vector<Branch> branches;
    std::vector<Commit> commits; // unordered union across all branches
    bool truncated = false;      // history/branches were capped while fetching
    std::vector<std::string> notes;
};

// The final, laid-out graph ready to be serialized to the frontend.
struct RepoGraph {
    Provider provider = Provider::Unknown;
    std::string owner;
    std::string repo;
    std::string defaultBranch;
    std::string webUrl;

    std::vector<Commit> commits; // ordered by row, ascending
    std::vector<Edge> edges;
    std::vector<Branch> branches;

    int laneCount = 0;
    bool truncated = false;
    std::vector<std::string> notes;
};

// Knobs controlling how much history we pull. Defaults keep us comfortably
// inside unauthenticated rate limits while still showing a meaningful graph.
struct FetchOptions {
    int maxPagesPerBranch = 10; // 100 commits per page
    int perPage = 100;
    int maxBranches = 60;
    std::string token;          // optional auth token (raises rate limits)
};

} // namespace gitst
