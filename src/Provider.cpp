#include "gitst/Provider.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "gitst/RepoUrl.h"
#include "gitst/TimeUtil.h"

namespace gitst {
namespace {

using json = nlohmann::json;

std::string jstr(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || it->is_null() || !it->is_string()) return {};
    return it->get<std::string>();
}

std::string firstLine(const std::string& s) {
    auto nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

std::string shortSha(const std::string& sha) {
    return sha.size() > 7 ? sha.substr(0, 7) : sha;
}

// Extracts a human-readable error message from a provider JSON error body.
std::string apiErrorMessage(const HttpResponse& resp) {
    try {
        json j = json::parse(resp.body);
        if (j.contains("message") && j["message"].is_string())
            return j["message"].get<std::string>();
        if (j.contains("error") && j["error"].is_string())
            return j["error"].get<std::string>();
    } catch (...) {
    }
    return "HTTP " + std::to_string(resp.status);
}

// ---------------------------------------------------------------------------
// GitHub
// ---------------------------------------------------------------------------
class GitHubProvider : public GitProvider {
public:
    explicit GitHubProvider(HttpClient& client) : client_(client) {}

    bool fetch(const RepoRef& ref, const FetchOptions& opts, RepoData& out,
               std::string& error) override {
        std::vector<std::string> headers = {
            "Accept: application/vnd.github+json",
            "X-GitHub-Api-Version: 2022-11-28",
        };
        if (!opts.token.empty()) headers.push_back("Authorization: Bearer " + opts.token);

        // Repository metadata (default branch + web URL).
        std::string repoUrl = ref.apiBase + "/repos/" + ref.owner + "/" + ref.repo;
        HttpResponse meta = client_.get(repoUrl, headers);
        if (!meta.ok()) {
            error = describeFailure(meta, ref);
            return false;
        }
        try {
            json j = json::parse(meta.body);
            out.defaultBranch = jstr(j, "default_branch");
            out.webUrl = jstr(j, "html_url");
        } catch (const std::exception& e) {
            error = std::string("failed to parse repository metadata: ") + e.what();
            return false;
        }
        if (out.webUrl.empty()) out.webUrl = ref.webBase;

        // Branches (paginated).
        if (!fetchBranches(ref, headers, opts, out, error)) return false;
        if (out.branches.empty()) {
            error = "repository has no branches";
            return false;
        }

        // Commits, branch by branch, unioned by sha.
        fetchCommits(ref, headers, opts, out);
        return true;
    }

private:
    static std::string describeFailure(const HttpResponse& resp, const RepoRef& ref) {
        if (!resp.error.empty()) return "network error: " + resp.error;
        if (resp.status == 404)
            return "repository not found (is '" + ref.owner + "/" + ref.repo +
                   "' correct and public?)";
        if (resp.status == 403 || resp.status == 429) {
            std::string msg = apiErrorMessage(resp);
            return "access denied / rate limited: " + msg +
                   " (set GITHUB_TOKEN to raise the limit)";
        }
        if (resp.status == 401) return "authentication failed (check GITHUB_TOKEN)";
        return "GitHub API error: " + apiErrorMessage(resp);
    }

    bool fetchBranches(const RepoRef& ref, const std::vector<std::string>& headers,
                       const FetchOptions& opts, RepoData& out, std::string& error) {
        for (int page = 1;; ++page) {
            std::string url = ref.apiBase + "/repos/" + ref.owner + "/" + ref.repo +
                              "/branches?per_page=100&page=" + std::to_string(page);
            HttpResponse resp = client_.get(url, headers);
            if (!resp.ok()) {
                error = describeFailure(resp, ref);
                return false;
            }
            json arr;
            try {
                arr = json::parse(resp.body);
            } catch (const std::exception& e) {
                error = std::string("failed to parse branches: ") + e.what();
                return false;
            }
            if (!arr.is_array() || arr.empty()) break;
            for (const auto& b : arr) {
                Branch br;
                br.name = jstr(b, "name");
                if (b.contains("commit")) br.tipSha = jstr(b["commit"], "sha");
                br.isDefault = (br.name == out.defaultBranch);
                if (!br.name.empty() && !br.tipSha.empty()) out.branches.push_back(br);
            }
            if (arr.size() < 100) break;
            if (page >= 10) { // hard ceiling: up to 1000 branches
                out.truncated = true;
                out.notes.push_back("branch list truncated at 1000 branches");
                break;
            }
        }
        // Default branch first so it claims lane 0.
        std::stable_sort(out.branches.begin(), out.branches.end(),
                         [](const Branch& a, const Branch& b) {
                             return a.isDefault && !b.isDefault;
                         });
        if (static_cast<int>(out.branches.size()) > opts.maxBranches) {
            out.branches.resize(opts.maxBranches);
            out.truncated = true;
            out.notes.push_back("showing the first " + std::to_string(opts.maxBranches) +
                                " branches");
        }
        return true;
    }

    void fetchCommits(const RepoRef& ref, const std::vector<std::string>& headers,
                      const FetchOptions& opts, RepoData& out) {
        std::unordered_set<std::string> seen;
        std::unordered_map<std::string, size_t> indexBySha;

        for (const auto& branch : out.branches) {
            for (int page = 1; page <= opts.maxPagesPerBranch; ++page) {
                std::string url = ref.apiBase + "/repos/" + ref.owner + "/" + ref.repo +
                                  "/commits?sha=" + urlEncode(branch.tipSha) +
                                  "&per_page=" + std::to_string(opts.perPage) +
                                  "&page=" + std::to_string(page);
                HttpResponse resp = client_.get(url, headers);
                if (!resp.ok()) {
                    out.notes.push_back("could not fully load history for branch '" +
                                        branch.name + "'");
                    break;
                }
                json arr;
                try {
                    arr = json::parse(resp.body);
                } catch (...) {
                    break;
                }
                if (!arr.is_array() || arr.empty()) break;

                int freshOnPage = 0;
                for (const auto& c : arr) {
                    Commit commit = parseCommit(c);
                    if (commit.sha.empty()) continue;
                    if (seen.insert(commit.sha).second) {
                        indexBySha[commit.sha] = out.commits.size();
                        out.commits.push_back(std::move(commit));
                        ++freshOnPage;
                    }
                }
                // If an entire page is already known, the rest of this branch's
                // history is shared with one we've fetched — stop early.
                if (freshOnPage == 0) break;
                if (arr.size() < static_cast<size_t>(opts.perPage)) break;
                if (page == opts.maxPagesPerBranch) {
                    out.truncated = true;
                }
            }
        }
        (void)indexBySha;
    }

    static Commit parseCommit(const json& c) {
        Commit out;
        out.sha = jstr(c, "sha");
        if (out.sha.empty()) return out;
        out.shortSha = shortSha(out.sha);
        out.url = jstr(c, "html_url");
        if (c.contains("commit")) {
            const auto& cm = c["commit"];
            out.message = jstr(cm, "message");
            out.summary = firstLine(out.message);
            if (cm.contains("author")) {
                out.authorName = jstr(cm["author"], "name");
                out.authorEmail = jstr(cm["author"], "email");
                out.authorDate = jstr(cm["author"], "date");
                out.authorTs = parseIso8601(out.authorDate);
            }
            if (cm.contains("committer")) {
                out.committerName = jstr(cm["committer"], "name");
                out.committerDate = jstr(cm["committer"], "date");
            }
        }
        if (c.contains("parents") && c["parents"].is_array()) {
            for (const auto& p : c["parents"]) {
                std::string psha = jstr(p, "sha");
                if (!psha.empty()) out.parents.push_back(psha);
            }
        }
        out.isMerge = out.parents.size() > 1;
        return out;
    }

    HttpClient& client_;
};

// ---------------------------------------------------------------------------
// GitLab
// ---------------------------------------------------------------------------
class GitLabProvider : public GitProvider {
public:
    explicit GitLabProvider(HttpClient& client) : client_(client) {}

    bool fetch(const RepoRef& ref, const FetchOptions& opts, RepoData& out,
               std::string& error) override {
        std::vector<std::string> headers;
        if (!opts.token.empty()) headers.push_back("PRIVATE-TOKEN: " + opts.token);

        std::string id = urlEncode(ref.projectPath);
        std::string projectUrl = ref.apiBase + "/projects/" + id;
        HttpResponse meta = client_.get(projectUrl, headers);
        if (!meta.ok()) {
            error = describeFailure(meta, ref);
            return false;
        }
        try {
            json j = json::parse(meta.body);
            out.defaultBranch = jstr(j, "default_branch");
            out.webUrl = jstr(j, "web_url");
        } catch (const std::exception& e) {
            error = std::string("failed to parse project metadata: ") + e.what();
            return false;
        }
        if (out.webUrl.empty()) out.webUrl = ref.webBase;

        if (!fetchBranches(ref, id, headers, opts, out, error)) return false;
        if (out.branches.empty()) {
            error = "project has no branches";
            return false;
        }
        fetchCommits(ref, id, headers, opts, out);
        return true;
    }

private:
    static std::string describeFailure(const HttpResponse& resp, const RepoRef& ref) {
        if (!resp.error.empty()) return "network error: " + resp.error;
        if (resp.status == 404)
            return "project not found (is '" + ref.projectPath +
                   "' correct and public?)";
        if (resp.status == 401 || resp.status == 403)
            return "access denied: " + apiErrorMessage(resp) +
                   " (set GITLAB_TOKEN for private projects)";
        if (resp.status == 429) return "rate limited by GitLab; try again shortly";
        return "GitLab API error: " + apiErrorMessage(resp);
    }

    bool fetchBranches(const RepoRef& ref, const std::string& id,
                       const std::vector<std::string>& headers,
                       const FetchOptions& opts, RepoData& out, std::string& error) {
        for (int page = 1;; ++page) {
            std::string url = ref.apiBase + "/projects/" + id +
                              "/repository/branches?per_page=100&page=" +
                              std::to_string(page);
            HttpResponse resp = client_.get(url, headers);
            if (!resp.ok()) {
                error = describeFailure(resp, ref);
                return false;
            }
            json arr;
            try {
                arr = json::parse(resp.body);
            } catch (const std::exception& e) {
                error = std::string("failed to parse branches: ") + e.what();
                return false;
            }
            if (!arr.is_array() || arr.empty()) break;
            for (const auto& b : arr) {
                Branch br;
                br.name = jstr(b, "name");
                if (b.contains("commit")) br.tipSha = jstr(b["commit"], "id");
                br.isDefault = (br.name == out.defaultBranch);
                if (!br.name.empty() && !br.tipSha.empty()) out.branches.push_back(br);
            }
            if (arr.size() < 100) break;
            if (page >= 10) {
                out.truncated = true;
                break;
            }
        }
        std::stable_sort(out.branches.begin(), out.branches.end(),
                         [](const Branch& a, const Branch& b) {
                             return a.isDefault && !b.isDefault;
                         });
        if (static_cast<int>(out.branches.size()) > opts.maxBranches) {
            out.branches.resize(opts.maxBranches);
            out.truncated = true;
            out.notes.push_back("showing the first " + std::to_string(opts.maxBranches) +
                                " branches");
        }
        return true;
    }

    void fetchCommits(const RepoRef& ref, const std::string& id,
                      const std::vector<std::string>& headers,
                      const FetchOptions& opts, RepoData& out) {
        std::unordered_set<std::string> seen;
        for (const auto& branch : out.branches) {
            for (int page = 1; page <= opts.maxPagesPerBranch; ++page) {
                std::string url = ref.apiBase + "/projects/" + id +
                                  "/repository/commits?ref_name=" + urlEncode(branch.name) +
                                  "&per_page=" + std::to_string(opts.perPage) +
                                  "&page=" + std::to_string(page);
                HttpResponse resp = client_.get(url, headers);
                if (!resp.ok()) {
                    out.notes.push_back("could not fully load history for branch '" +
                                        branch.name + "'");
                    break;
                }
                json arr;
                try {
                    arr = json::parse(resp.body);
                } catch (...) {
                    break;
                }
                if (!arr.is_array() || arr.empty()) break;

                int freshOnPage = 0;
                for (const auto& c : arr) {
                    Commit commit = parseCommit(c, ref);
                    if (commit.sha.empty()) continue;
                    if (seen.insert(commit.sha).second) {
                        out.commits.push_back(std::move(commit));
                        ++freshOnPage;
                    }
                }
                if (freshOnPage == 0) break;
                if (arr.size() < static_cast<size_t>(opts.perPage)) break;
                if (page == opts.maxPagesPerBranch) out.truncated = true;
            }
        }
    }

    static Commit parseCommit(const json& c, const RepoRef& ref) {
        Commit out;
        out.sha = jstr(c, "id");
        if (out.sha.empty()) return out;
        out.shortSha = shortSha(out.sha);
        out.message = jstr(c, "message");
        out.summary = jstr(c, "title");
        if (out.summary.empty()) out.summary = firstLine(out.message);
        out.authorName = jstr(c, "author_name");
        out.authorEmail = jstr(c, "author_email");
        out.authorDate = jstr(c, "authored_date");
        out.authorTs = parseIso8601(out.authorDate);
        out.committerName = jstr(c, "committer_name");
        out.committerDate = jstr(c, "committed_date");
        out.url = jstr(c, "web_url");
        if (out.url.empty()) out.url = ref.webBase + "/-/commit/" + out.sha;
        if (c.contains("parent_ids") && c["parent_ids"].is_array()) {
            for (const auto& p : c["parent_ids"]) {
                if (p.is_string()) out.parents.push_back(p.get<std::string>());
            }
        }
        out.isMerge = out.parents.size() > 1;
        return out;
    }

    HttpClient& client_;
};

} // namespace

std::unique_ptr<GitProvider> makeProvider(Provider provider, HttpClient& client) {
    switch (provider) {
        case Provider::GitHub: return std::make_unique<GitHubProvider>(client);
        case Provider::GitLab: return std::make_unique<GitLabProvider>(client);
        default: return nullptr;
    }
}

} // namespace gitst
