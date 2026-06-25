#include "gitst/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

namespace gitst {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// RAII for a WinHTTP handle.
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

std::string lastErrorMessage(const char* stage) {
    DWORD code = GetLastError();
    return std::string(stage) + " failed (WinHTTP error " + std::to_string(code) + ")";
}

} // namespace

HttpResponse HttpClient::get(const std::string& url,
                             const std::vector<std::string>& headers) {
    HttpResponse resp;
    std::wstring wurl = widen(url);

    // Split the URL into components.
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    wchar_t path[4096] = {0};
    uc.lpszHostName = host;     uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;      uc.dwUrlPathLength = 4095;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        resp.error = lastErrorMessage("URL parse");
        return resp;
    }
    bool secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    INTERNET_PORT port = uc.nPort ? uc.nPort
                                  : (secure ? INTERNET_DEFAULT_HTTPS_PORT
                                            : INTERNET_DEFAULT_HTTP_PORT);

    Handle session(WinHttpOpen(widen(userAgent_).c_str(),
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { resp.error = lastErrorMessage("WinHttpOpen"); return resp; }

    // Be generous with timeouts; large repos can be slow to page through.
    WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 30000);

    Handle connect(WinHttpConnect(session.h, host, port, 0));
    if (!connect) { resp.error = lastErrorMessage("WinHttpConnect"); return resp; }

    DWORD flags = WINHTTP_FLAG_REFRESH | (secure ? WINHTTP_FLAG_SECURE : 0);
    Handle request(WinHttpOpenRequest(connect.h, L"GET", path, nullptr,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) { resp.error = lastErrorMessage("WinHttpOpenRequest"); return resp; }

    // Transparently decompress gzip/deflate when the server uses it.
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));

    // Follow redirects automatically (default), and add caller headers.
    for (const auto& h : headers) {
        std::wstring wh = widen(h) + L"\r\n";
        WinHttpAddRequestHeaders(request.h, wh.c_str(), (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        resp.error = lastErrorMessage("WinHttpSendRequest");
        return resp;
    }
    if (!WinHttpReceiveResponse(request.h, nullptr)) {
        resp.error = lastErrorMessage("WinHttpReceiveResponse");
        return resp;
    }

    // Status code.
    DWORD statusCode = 0, len = sizeof(statusCode);
    WinHttpQueryHeaders(request.h,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &len,
                        WINHTTP_NO_HEADER_INDEX);
    resp.status = static_cast<long>(statusCode);

    // Body.
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) {
            resp.error = lastErrorMessage("WinHttpQueryDataAvailable");
            return resp;
        }
        if (avail == 0) break;
        size_t offset = resp.body.size();
        resp.body.resize(offset + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, &resp.body[offset], avail, &read)) {
            resp.error = lastErrorMessage("WinHttpReadData");
            return resp;
        }
        resp.body.resize(offset + read);
        if (read == 0) break;
    }

    return resp;
}

} // namespace gitst
