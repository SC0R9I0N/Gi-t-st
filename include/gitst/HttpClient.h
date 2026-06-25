#pragma once

#include <map>
#include <string>
#include <vector>

namespace gitst {

struct HttpResponse {
    long status = 0;                 // HTTP status code, 0 on transport failure
    std::string body;                // raw response body (UTF-8)
    std::string error;               // non-empty when the request itself failed

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// HTTPS GET client backed by WinHTTP (a Windows system component, so no
// third-party networking DLLs are required).
class HttpClient {
public:
    HttpClient() = default;

    // Issues a GET. `headers` are raw "Key: Value" strings (ASCII). Transport
    // errors are reported via HttpResponse::error rather than thrown.
    HttpResponse get(const std::string& url,
                     const std::vector<std::string>& headers = {});

    void setUserAgent(std::string ua) { userAgent_ = std::move(ua); }

private:
    std::string userAgent_ = "Gi-t-st/1.0";
};

} // namespace gitst
