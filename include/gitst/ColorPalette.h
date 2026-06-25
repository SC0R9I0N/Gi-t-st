#pragma once

#include <string>

namespace gitst {

// Branch coloring. The main/master (default) branch always uses one fixed,
// reserved color so it is instantly recognizable across any repository. All
// other lanes draw from a palette of visually distinct hues chosen to avoid
// clashing with the reserved main color.
namespace colors {

// Fixed color for the default branch's first-parent mainline.
const std::string& mainline();

// Deterministic color for a non-mainline lane index.
const std::string& forLane(int laneIndex);

// Number of distinct palette entries (lanes wrap around this).
int paletteSize();

} // namespace colors
} // namespace gitst
