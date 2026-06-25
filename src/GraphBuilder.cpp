#include "gitst/GraphBuilder.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "gitst/ColorPalette.h"

namespace gitst {
namespace {

// Index into the commit vector, plus the commit's tie-break key, for the
// topological-order priority queue. We emit children before parents and, among
// commits that are ready, the most recent one first (matching `git log` order).
struct ReadyNode {
    std::int64_t ts;
    std::string sha;
    size_t index;
};

struct ReadyCompare {
    bool operator()(const ReadyNode& a, const ReadyNode& b) const {
        if (a.ts != b.ts) return a.ts < b.ts;       // newer first
        return a.sha < b.sha;                        // deterministic tie-break
    }
};

// Assigns each commit a row in topological order. A commit is only emitted once
// all of its children (that exist in our set) have been emitted, guaranteeing a
// child always sits above its parents.
std::vector<size_t> topologicalOrder(
    std::vector<Commit>& commits,
    const std::unordered_map<std::string, size_t>& indexBySha) {

    const size_t n = commits.size();
    std::vector<int> remainingChildren(n, 0);

    // childCount[parent] = number of present commits listing it as a parent.
    for (const auto& c : commits) {
        for (const auto& p : c.parents) {
            auto it = indexBySha.find(p);
            if (it != indexBySha.end()) remainingChildren[it->second]++;
        }
    }

    std::priority_queue<ReadyNode, std::vector<ReadyNode>, ReadyCompare> ready;
    for (size_t i = 0; i < n; ++i) {
        if (remainingChildren[i] == 0)
            ready.push({commits[i].authorTs, commits[i].sha, i});
    }

    std::vector<size_t> order;
    order.reserve(n);
    while (!ready.empty()) {
        ReadyNode node = ready.top();
        ready.pop();
        order.push_back(node.index);
        for (const auto& p : commits[node.index].parents) {
            auto it = indexBySha.find(p);
            if (it == indexBySha.end()) continue;
            if (--remainingChildren[it->second] == 0)
                ready.push({commits[it->second].authorTs, commits[it->second].sha,
                            it->second});
        }
    }

    // Safety net for pathological inputs (e.g. a cycle from bad data): append
    // anything that never became ready, ordered by timestamp.
    if (order.size() < n) {
        std::unordered_set<size_t> placed(order.begin(), order.end());
        std::vector<size_t> leftover;
        for (size_t i = 0; i < n; ++i)
            if (!placed.count(i)) leftover.push_back(i);
        std::sort(leftover.begin(), leftover.end(), [&](size_t a, size_t b) {
            return commits[a].authorTs > commits[b].authorTs;
        });
        order.insert(order.end(), leftover.begin(), leftover.end());
    }

    return order;
}

// First free lane (an empty slot), or a new lane appended at the end.
int allocateLane(std::vector<std::string>& lanes, const std::string& sha) {
    for (size_t i = 0; i < lanes.size(); ++i) {
        if (lanes[i].empty()) {
            lanes[i] = sha;
            return static_cast<int>(i);
        }
    }
    lanes.push_back(sha);
    return static_cast<int>(lanes.size() - 1);
}

} // namespace

RepoGraph buildGraph(const RepoRef& ref, RepoData&& data) {
    RepoGraph graph;
    graph.provider = ref.provider;
    graph.owner = ref.owner;
    graph.repo = ref.repo;
    graph.defaultBranch = data.defaultBranch;
    graph.webUrl = data.webUrl.empty() ? ref.webBase : data.webUrl;
    graph.truncated = data.truncated;
    graph.notes = data.notes;

    std::vector<Commit> commits = std::move(data.commits);
    if (commits.empty()) {
        graph.branches = data.branches;
        return graph;
    }

    // Build a sha -> index map for O(1) parent/child lookups.
    std::unordered_map<std::string, size_t> indexBySha;
    indexBySha.reserve(commits.size() * 2);
    for (size_t i = 0; i < commits.size(); ++i) indexBySha[commits[i].sha] = i;

    // 1. Order commits into rows (children above parents, newest first).
    std::vector<size_t> order = topologicalOrder(commits, indexBySha);

    // 2. Lane assignment. Process top-to-bottom; `lanes[i]` holds the sha the
    //    lane is currently waiting to place (empty == free). First parents keep
    //    a commit's own lane so mainline history draws as a straight column.
    std::vector<std::string> lanes;
    std::vector<int> rowToIndex(order.size());

    for (size_t row = 0; row < order.size(); ++row) {
        size_t idx = order[row];
        Commit& c = commits[idx];
        c.row = static_cast<int>(row);
        rowToIndex[row] = static_cast<int>(idx);

        // Find the lane that expected this commit, else take a fresh one.
        int lane = -1;
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (lanes[i] == c.sha) { lane = static_cast<int>(i); break; }
        }
        if (lane == -1) lane = allocateLane(lanes, c.sha);
        c.lane = lane;

        // Any *other* lane also expecting this sha converges here; free it.
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (static_cast<int>(i) != lane && lanes[i] == c.sha) lanes[i].clear();
        }

        // Set up lanes for the present parents.
        bool firstHandled = false;
        for (const auto& p : c.parents) {
            if (!indexBySha.count(p)) continue; // parent outside fetched history
            if (!firstHandled) {
                lanes[lane] = p;     // first parent continues this lane
                firstHandled = true;
            } else {
                // Merge parent: reuse a lane already waiting on it, else a new one.
                bool found = false;
                for (auto& slot : lanes) {
                    if (slot == p) { found = true; break; }
                }
                if (!found) allocateLane(lanes, p);
            }
        }
        if (!firstHandled) lanes[lane].clear(); // root / fully-truncated commit
    }
    graph.laneCount = static_cast<int>(lanes.size());

    // 3. Coloring. Reserve the fixed mainline color for the default branch's
    //    first-parent chain; everything else is colored by its lane.
    std::string defaultTip;
    for (const auto& b : data.branches) {
        if (b.isDefault) { defaultTip = b.tipSha; break; }
    }
    if (defaultTip.empty() && !data.branches.empty())
        defaultTip = data.branches.front().tipSha;

    std::unordered_set<std::string> mainline;
    {
        std::string cur = defaultTip;
        while (!cur.empty() && mainline.insert(cur).second) {
            auto it = indexBySha.find(cur);
            if (it == indexBySha.end()) break;
            const Commit& c = commits[it->second];
            cur = c.parents.empty() ? std::string() : c.parents.front();
        }
    }

    for (auto& c : commits) {
        c.onMainline = mainline.count(c.sha) > 0;
        c.color = c.onMainline ? colors::mainline() : colors::forLane(c.lane);
    }

    // 4. Branch head tags + legend colors.
    for (auto& b : data.branches) {
        auto it = indexBySha.find(b.tipSha);
        if (it != indexBySha.end()) {
            commits[it->second].branchHeads.push_back(b.name);
            b.color = b.isDefault ? colors::mainline() : commits[it->second].color;
        } else {
            b.color = b.isDefault ? colors::mainline() : colors::forLane(0);
        }
    }
    graph.branches = data.branches;

    // 5. Edges, with geometry resolved from final placements. Merge edges adopt
    //    the merged-in branch's color; first-parent edges follow the child.
    for (const auto& c : commits) {
        bool first = true;
        for (const auto& p : c.parents) {
            auto it = indexBySha.find(p);
            bool present = it != indexBySha.end();
            Edge e;
            e.from = c.sha;
            e.to = p;
            e.fromRow = c.row;
            e.fromLane = c.lane;
            e.merge = !first;
            if (present) {
                const Commit& parent = commits[it->second];
                e.toRow = parent.row;
                e.toLane = parent.lane;
                e.color = e.merge ? parent.color : c.color;
            } else {
                // Parent is beyond the fetched window: draw a short downward stub.
                e.toRow = -1;
                e.toLane = c.lane;
                e.color = c.color;
            }
            graph.edges.push_back(e);
            first = false;
        }
    }

    // 6. Emit commits in row order.
    graph.commits.reserve(commits.size());
    for (size_t row = 0; row < rowToIndex.size(); ++row) {
        graph.commits.push_back(std::move(commits[rowToIndex[row]]));
    }

    return graph;
}

} // namespace gitst
