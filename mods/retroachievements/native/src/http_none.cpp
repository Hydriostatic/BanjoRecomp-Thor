// Fallback for desktop builds without libcurl: the mod loads, but can't go online.

#if !defined(__ANDROID__) && !defined(RA_HTTP_CURL)

#include "http.h"

bool ra_http::init(std::string& error) {
    error = "this build has no HTTP support (built without libcurl)";
    return false;
}

bool ra_http::using_bridge() {
    return false;
}

void ra_http::send_async(Request, std::function<void(Response)> done) {
    done(Response{});
}

#endif
