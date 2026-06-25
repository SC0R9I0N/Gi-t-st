#include "gitst/ColorPalette.h"

#include <array>

namespace gitst {
namespace colors {
namespace {

// A fixed, reserved color for the default branch. Chosen to read as "the trunk".
const std::string kMainline = "#4f9dff";

// Distinct hues for feature/topic lanes. Deliberately excludes the mainline
// blue so the default branch never blends into a neighbouring lane.
const std::array<std::string, 10> kPalette = {{
    "#e5534b", // red
    "#57ab5a", // green
    "#c69026", // amber
    "#a371f7", // purple
    "#ec775c", // coral
    "#39c5cf", // teal
    "#db61a2", // pink
    "#8ddb8c", // light green
    "#daaa3f", // gold
    "#986ee2", // violet
}};

} // namespace

const std::string& mainline() { return kMainline; }

const std::string& forLane(int laneIndex) {
    if (laneIndex < 0) laneIndex = 0;
    return kPalette[static_cast<size_t>(laneIndex) % kPalette.size()];
}

int paletteSize() { return static_cast<int>(kPalette.size()); }

} // namespace colors
} // namespace gitst
