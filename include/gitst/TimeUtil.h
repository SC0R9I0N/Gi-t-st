#pragma once

#include <cstdint>
#include <string>

namespace gitst {

// Parses an ISO-8601 timestamp (e.g. "2023-04-05T06:07:08Z" or
// "2023-04-05T06:07:08.000+02:00") into a unix timestamp in UTC seconds.
// Returns 0 if the string cannot be understood. Timezone-table free and
// portable across platforms (no timegm / _mkgmtime dependency).
std::int64_t parseIso8601(const std::string& s);

} // namespace gitst
