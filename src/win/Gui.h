#pragma once

// Centralized Win32 + GDI+ includes and small shared helpers. Including this
// first keeps the GDI+ <-> NOMINMAX min/max workaround in one place.
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>   // PROPID etc. required by <gdiplus.h> on MinGW

#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

#include <string>

#include "gitst/Models.h"

namespace gitst {
namespace gui {

// ---- Layout constants (logical pixels; scaled by DPI at runtime) -----------
namespace ui {
constexpr int kTopBarH   = 58;
constexpr int kDetailsW  = 360;
constexpr int kLeftPad   = 26;   // graph: x of lane 0
constexpr int kLaneW     = 20;   // graph: horizontal spacing between lanes
constexpr int kNodeR     = 5;    // graph: commit dot radius
constexpr int kRowH      = 38;   // graph: default row height
constexpr int kMinRowH   = 22;
constexpr int kMaxRowH   = 64;
} // namespace ui

// ---- String helpers --------------------------------------------------------
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);
std::wstring relativeTime(long long unixTs);
std::wstring formatLocalDate(const std::string& iso);

// ---- Color helpers ---------------------------------------------------------
Gdiplus::Color parseHexColor(const std::string& hex, BYTE alpha = 255);

// ---- Shared application model ---------------------------------------------
// Owned by the main window; the graph and details views hold a pointer to it.
struct AppModel {
    RepoGraph graph;
    bool hasGraph = false;     // a successful load has populated `graph`
    bool loading = false;      // a fetch is in flight
    std::string error;         // last failure (shown only after an attempt)
    std::string status;        // transient status line for the top bar
    std::string selectedSha;   // currently inspected commit (empty == none)
};

// Callback surface the child views use to drive the app.
class AppHost {
public:
    virtual ~AppHost() = default;
    virtual void selectCommit(const std::string& sha, bool scrollIntoView) = 0;
    virtual void clearSelection() = 0;
    virtual void openExternal(const std::string& url) = 0;
};

// ---- DPI scaling -----------------------------------------------------------
// Set once per window from its current DPI; helpers scale logical px.
extern int g_dpi;
inline int dp(int logical) { return MulDiv(logical, g_dpi, 96); }

} // namespace gui
} // namespace gitst
