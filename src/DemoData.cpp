#include "gitst/DemoData.h"

#include "gitst/TimeUtil.h"

namespace gitst {
namespace {

// Helper to make a commit with deterministic, readable fake data.
Commit mk(const std::string& sha, const std::string& summary,
          const std::string& author, const std::string& date,
          std::vector<std::string> parents) {
    Commit c;
    c.sha = sha + std::string(40 - sha.size(), '0'); // pad to a sha-ish length
    c.shortSha = c.sha.substr(0, 7);
    c.summary = summary;
    c.message = summary + "\n\nPart of the offline demo repository.";
    c.authorName = author;
    c.authorEmail = author == "Ada Lovelace" ? "ada@example.com" : "grace@example.com";
    c.authorDate = date;
    c.authorTs = parseIso8601(date);
    c.committerName = author;
    c.committerDate = date;
    for (auto& p : parents) c.parents.push_back(p + std::string(40 - p.size(), '0'));
    c.isMerge = c.parents.size() > 1;
    c.url = "https://example.com/demo/commit/" + c.sha;
    return c;
}

} // namespace

RepoData makeDemoRepo() {
    RepoData d;
    d.defaultBranch = "main";
    d.webUrl = "https://example.com/demo/repo";
    d.notes.push_back("This is a built-in offline demo. Paste a real repo URL to fetch live data.");

    // A small history with two feature branches and two merges back into main.
    //
    //   main:    c1 - c2 -------- m1 ------- c6 ---- m2
    //                   \        /                  /
    //   feature:         f1 - f2                   /
    //                                             /
    //   hotfix:                      c2 - h1 ----
    d.commits.push_back(mk("c1", "Initial project scaffold", "Ada Lovelace", "2024-01-02T09:00:00Z", {}));
    d.commits.push_back(mk("c2", "Add README and license", "Ada Lovelace", "2024-01-03T11:30:00Z", {"c1"}));
    d.commits.push_back(mk("f1", "Start search feature", "Grace Hopper", "2024-01-04T14:10:00Z", {"c2"}));
    d.commits.push_back(mk("f2", "Implement fuzzy search", "Grace Hopper", "2024-01-06T16:45:00Z", {"f1"}));
    d.commits.push_back(mk("m1", "Merge feature/search", "Ada Lovelace", "2024-01-07T10:00:00Z", {"c2", "f2"}));
    d.commits.push_back(mk("h1", "Hotfix: null pointer on startup", "Grace Hopper", "2024-01-05T08:20:00Z", {"c2"}));
    d.commits.push_back(mk("c6", "Polish settings screen", "Ada Lovelace", "2024-01-08T13:00:00Z", {"m1"}));
    d.commits.push_back(mk("m2", "Merge hotfix into main", "Ada Lovelace", "2024-01-09T09:30:00Z", {"c6", "h1"}));

    auto padded = [](const std::string& s) { return s + std::string(40 - s.size(), '0'); };
    d.branches.push_back({"main", padded("m2"), true, ""});
    d.branches.push_back({"feature/search", padded("f2"), false, ""});
    d.branches.push_back({"hotfix/startup-crash", padded("h1"), false, ""});

    return d;
}

} // namespace gitst
