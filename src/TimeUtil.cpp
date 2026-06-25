#include "gitst/TimeUtil.h"

#include <cctype>
#include <cstdlib>

namespace gitst {
namespace {

// Days since the unix epoch for a given civil date (Howard Hinnant's algorithm).
// Works for any valid Gregorian date without relying on platform timezone APIs.
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= (m <= 2);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool readInt(const std::string& s, size_t pos, size_t len, int& out) {
    if (pos + len > s.size()) return false;
    int v = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = s[pos + i];
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

} // namespace

std::int64_t parseIso8601(const std::string& s) {
    // Minimum shape: YYYY-MM-DDTHH:MM:SS
    if (s.size() < 19) return 0;

    int year, mon, day, hour, min, sec;
    if (!readInt(s, 0, 4, year) || !readInt(s, 5, 2, mon) ||
        !readInt(s, 8, 2, day) || !readInt(s, 11, 2, hour) ||
        !readInt(s, 14, 2, min) || !readInt(s, 17, 2, sec)) {
        return 0;
    }

    std::int64_t days = daysFromCivil(year, static_cast<unsigned>(mon),
                                      static_cast<unsigned>(day));
    std::int64_t epoch = days * 86400 + hour * 3600 + min * 60 + sec;

    // Skip an optional fractional-seconds component.
    size_t i = 19;
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }

    // Apply an optional timezone offset ("Z" == UTC == no offset).
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        int sign = (s[i] == '+') ? 1 : -1;
        int oh = 0, om = 0;
        if (readInt(s, i + 1, 2, oh)) {
            // Minutes may be separated by ':' or run together.
            size_t mpos = (i + 3 < s.size() && s[i + 3] == ':') ? i + 4 : i + 3;
            readInt(s, mpos, 2, om);
            epoch -= sign * (oh * 3600 + om * 60);
        }
    }

    return epoch;
}

} // namespace gitst
