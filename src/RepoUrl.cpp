#include "gitst/RepoUrl.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace gitst {

const char* providerName(Provider p) {
    switch (p) {
        case Provider::GitHub: return "github";
        case Provider::GitLab: return "gitlab";
        default: return "unknown";
    }
}

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

void trim(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}

// Splits "host/path/parts" given a raw input, normalizing the various URL
// styles (https, scp-like git@, bare host/path, bare owner/repo) into a host
// plus a "/"-joined path. Host is empty when none was supplied.
bool splitHostAndPath(const std::string& raw, std::string& host, std::string& path) {
    std::string s = raw;

    // scp-like syntax: git@host:owner/repo(.git)
    auto at = s.find('@');
    auto scheme = s.find("://");
    if (scheme == std::string::npos && at != std::string::npos) {
        auto colon = s.find(':', at);
        if (colon != std::string::npos) {
            host = s.substr(at + 1, colon - at - 1);
            path = s.substr(colon + 1);
            return true;
        }
    }

    // Strip a scheme if present.
    if (scheme != std::string::npos) s = s.substr(scheme + 3);

    // Strip userinfo (user@host).
    at = s.find('@');
    auto slash = s.find('/');
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
        s = s.substr(at + 1);
    }

    slash = s.find('/');
    if (slash == std::string::npos) {
        // No host segment — treat the whole thing as a path (owner/repo).
        host.clear();
        path = s;
        return !path.empty();
    }

    std::string first = s.substr(0, slash);
    // Heuristic: a leading segment with a dot or "localhost" is a hostname.
    if (first.find('.') != std::string::npos || toLower(first) == "localhost") {
        host = first;
        path = s.substr(slash + 1);
    } else {
        host.clear();
        path = s;
    }
    return !path.empty();
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    std::stringstream ss(path);
    while (std::getline(ss, cur, '/')) {
        if (!cur.empty()) parts.push_back(cur);
    }
    return parts;
}

Provider detectProvider(const std::string& host) {
    std::string h = toLower(host);
    if (h.find("gitlab") != std::string::npos) return Provider::GitLab;
    if (h.find("github") != std::string::npos) return Provider::GitHub;
    return Provider::Unknown;
}

} // namespace

std::string urlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

ParsedUrl parseRepoUrl(const std::string& input) {
    ParsedUrl result;
    std::string raw = input;
    trim(raw);
    if (raw.empty()) {
        result.error = "empty repository URL";
        return result;
    }

    std::string host, path;
    if (!splitHostAndPath(raw, host, path)) {
        result.error = "could not parse a repository path from the input";
        return result;
    }

    // Strip trailing ".git" and any query/fragment.
    auto cut = path.find_first_of("?#");
    if (cut != std::string::npos) path = path.substr(0, cut);
    if (path.size() >= 4 && toLower(path.substr(path.size() - 4)) == ".git") {
        path = path.substr(0, path.size() - 4);
    }
    while (!path.empty() && path.back() == '/') path.pop_back();

    auto parts = splitPath(path);
    if (parts.size() < 2) {
        result.error = "expected at least 'owner/repo' in the URL";
        return result;
    }

    if (host.empty()) host = "github.com"; // bare owner/repo defaults to GitHub
    Provider provider = detectProvider(host);

    RepoRef ref;
    ref.host = host;
    ref.provider = provider;

    if (provider == Provider::GitLab) {
        // GitLab projects may be nested in subgroups: the whole path is the id.
        ref.projectPath = path;
        ref.owner = parts.front();
        ref.repo = parts.back();
        ref.apiBase = "https://" + host + "/api/v4";
        ref.webBase = "https://" + host + "/" + path;
    } else {
        // GitHub (and GitHub Enterprise) use a flat owner/repo.
        ref.owner = parts[0];
        ref.repo = parts[1];
        ref.projectPath = ref.owner + "/" + ref.repo;
        if (provider == Provider::Unknown) {
            // Unknown host: assume a GitHub-compatible API and tell the user.
            ref.provider = Provider::GitHub;
            provider = Provider::GitHub;
        }
        if (toLower(host) == "github.com") {
            ref.apiBase = "https://api.github.com";
        } else {
            ref.apiBase = "https://" + host + "/api/v3"; // GH Enterprise
        }
        ref.webBase = "https://" + host + "/" + ref.owner + "/" + ref.repo;
    }

    result.ok = true;
    result.ref = ref;
    return result;
}

} // namespace gitst
