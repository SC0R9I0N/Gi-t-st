# Gi(t/st)

**Gi(t/st)** is a native Windows desktop application that visualizes a Git
repository's branch history. Paste a GitHub or GitLab link and it renders the
full commit history across **all branches** as a clean, navigable graph: every
branch is color-coded, the default (`main`/`master`) branch always keeps the
same blue, and you can click any commit or merge node to inspect its metadata.

It is a self-contained **C++17** GUI executable — no browser, no bundled
runtime, no third-party DLLs. The UI is drawn with **GDI+**, repositories are
fetched over **WinHTTP**, and the graph layout (topological ordering, lane
assignment, and coloring) is computed entirely in C++.

---

## Features

- **GitHub & GitLab** — paste almost any URL form (`https://…`, `git@…:…`,
  `owner/repo`, GitLab subgroups, self-hosted GitHub Enterprise / GitLab hosts).
- **Full multi-branch graph** — commits from every branch are unioned into a
  single DAG and laid out the way mature history viewers do: first-parent
  chains stay in a straight column, merges and branches curve cleanly.
- **Consistent coloring** — the default branch's mainline always uses one
  reserved blue; other lanes draw from a distinct palette. A legend across the
  top maps colors to branches (click a branch to jump to its tip).
- **Click any node** — a details panel shows the message, author + email,
  author/commit timestamps, parent commits (click to navigate), branch tips,
  and a link that opens the commit in your browser.
- **Smooth & responsive** — the graph is custom-painted and virtualized
  (only visible rows are drawn), so even repositories with thousands of commits
  scroll smoothly. Pan by scrolling, zoom with `Ctrl` + scroll. Fetching runs
  on a background thread so the UI never freezes.
- **Empty by default** — on launch it simply shows "No repository loaded"; it
  never pretends a fetch failed before you've asked for one.
- **Offline demo** — the **Load demo** button renders a built-in synthetic
  repository with no network access required.

## Architecture

```
        ┌──────────────┐  WinHTTP  ┌────────────────────┐
repo URL│  RepoUrl     │ ────────► │ GitHub / GitLab API │
   ────►│  parsing     │           └─────────┬──────────┘
        └──────┬───────┘                     │ JSON (nlohmann/json)
               ▼                             ▼
        ┌──────────────┐           ┌────────────────────┐
        │  Provider    │ ────────► │  GraphBuilder       │
        │  (fetch DAG) │  RepoData │  topo + lanes +     │
        └──────────────┘           │  colors + edges     │
                                   └─────────┬───────────┘
                                             │ RepoGraph
                                             ▼
        ┌─────────────────────────────────────────────────┐
        │ Win32 window  →  GraphView + DetailsPanel (GDI+) │
        └─────────────────────────────────────────────────┘
```

| Layer | File(s) |
|-------|---------|
| URL parsing / provider detection | `src/RepoUrl.cpp` |
| HTTPS client (WinHTTP) | `src/HttpClient.cpp` |
| Provider fetching (GitHub/GitLab) | `src/Provider.cpp` |
| Graph layout (topo, lanes, colors) | `src/GraphBuilder.cpp` |
| Color palette | `src/ColorPalette.cpp` |
| Fetch orchestration | `src/Fetcher.cpp` |
| Window / top bar / legend / async | `src/main.cpp` |
| Graph rendering & interaction | `src/win/GraphView.cpp` |
| Commit details panel | `src/win/DetailsPanel.cpp` |
| Shared GUI helpers (fonts, colors, DPI) | `src/win/Gui.cpp` |

## Building

### Requirements

- **Windows 10/11**
- A C++17 compiler (MSVC, or MinGW-w64 / MSYS2)
- CMake ≥ 3.16
- Network access at configure time (CMake `FetchContent` pulls the header-only
  [nlohmann/json](https://github.com/nlohmann/json))

Everything else — GDI+, WinHTTP, the Win32 API — is part of Windows, so the
resulting `gitst.exe` is fully self-contained.

### MSYS2 / MinGW-w64

```bash
# In an MSYS2 MinGW64 shell (or with C:\msys64\mingw64\bin on PATH):
cmake -B build -S . -G Ninja
cmake --build build
./build/bin/gitst.exe
```

### MSVC / Visual Studio

```powershell
cmake -B build -S .
cmake --build build --config Release
.\build\bin\gitst.exe
```

In **CLion**, just open the project and run the `gitst` target (it uses the
bundled MinGW or your configured MSVC toolchain).

## Usage

Launch `gitst.exe`, paste a repository URL, and press **Visualize** (or
**Enter**). Click **Load demo** to explore offline.

- **Scroll** to pan; **Ctrl + scroll** to zoom; **Shift + scroll** to pan
  horizontally.
- **Click a commit** to open its details. In the panel, click a **parent**
  chip to jump to it, or the link to open the commit in your browser.
- **Click a branch** in the legend to jump to its tip.

### API rate limits

Unauthenticated GitHub/GitLab API access is rate-limited. To raise the limit
(and to read private repositories you have access to), set a token in your
environment before launching:

```
set GITHUB_TOKEN=ghp_xxx
set GITLAB_TOKEN=glpat-xxx
```

By default Gi(t/st) fetches up to 1000 commits per branch across up to 60
branches; the graph reports itself as *truncated* when those limits are hit.

## License

MIT — see `LICENSE`.
