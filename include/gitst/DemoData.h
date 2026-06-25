#pragma once

#include "gitst/Models.h"

namespace gitst {

// Builds a small synthetic repository (several branches, a couple of merges)
// so the UI can be explored offline, without any network access or API tokens.
RepoData makeDemoRepo();

} // namespace gitst
