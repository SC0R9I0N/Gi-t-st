#include "win/Gui.h"

#include <ctime>

#include "gitst/TimeUtil.h"

namespace gitst {
namespace gui {

int g_dpi = 96;

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                                nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring relativeTime(long long ts) {
    if (ts <= 0) return {};
    long long now = static_cast<long long>(::time(nullptr));
    long long secs = now - ts;
    if (secs < 0) return L"in the future";
    struct U { long long s; const wchar_t* name; };
    static const U units[] = {
        {31536000, L"year"}, {2592000, L"month"}, {604800, L"week"},
        {86400, L"day"},     {3600, L"hour"},     {60, L"minute"},
    };
    for (const auto& u : units) {
        long long v = secs / u.s;
        if (v >= 1) {
            std::wstring out = std::to_wstring(v) + L" " + u.name;
            if (v > 1) out += L"s";
            return out + L" ago";
        }
    }
    return L"just now";
}

std::wstring formatLocalDate(const std::string& iso) {
    long long ts = parseIso8601(iso);
    if (ts <= 0) return widen(iso);
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    tmv = *std::localtime(&t);
#endif
    wchar_t buf[64];
    if (std::wcsftime(buf, 64, L"%b %d, %Y  %H:%M", &tmv) == 0) return widen(iso);
    return buf;
}

Gdiplus::Color parseHexColor(const std::string& hex, BYTE alpha) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    size_t i = (!hex.empty() && hex[0] == '#') ? 1 : 0;
    if (hex.size() < i + 6) return Gdiplus::Color(alpha, 140, 150, 160);
    int r = hexVal(hex[i]) * 16 + hexVal(hex[i + 1]);
    int g = hexVal(hex[i + 2]) * 16 + hexVal(hex[i + 3]);
    int b = hexVal(hex[i + 4]) * 16 + hexVal(hex[i + 5]);
    return Gdiplus::Color(alpha, (BYTE)r, (BYTE)g, (BYTE)b);
}

} // namespace gui
} // namespace gitst
