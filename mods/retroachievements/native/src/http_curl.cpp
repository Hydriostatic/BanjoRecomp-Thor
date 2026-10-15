// HTTP on desktop builds, through libcurl. Only built when CMake finds curl; otherwise
// http_none.cpp is used and the mod reports that it can't go online.

#if !defined(__ANDROID__) && defined(RA_HTTP_CURL)

#include "http.h"

#include <curl/curl.h>
#include <rc_api_request.h>

#include <thread>

namespace {

size_t write_body(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

}

bool ra_http::init(std::string& error) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        error = "curl_global_init failed";
        return false;
    }
    return true;
}

bool ra_http::using_bridge() {
    return false;
}

void ra_http::send_async(Request request, std::function<void(Response)> done) {
    std::thread([request = std::move(request), done = std::move(done)]() {
        Response response;
        response.status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;

        CURL* curl = curl_easy_init();
        if (curl != nullptr) {
            struct curl_slist* headers = nullptr;
            curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
            curl_easy_setopt(curl, CURLOPT_USERAGENT, request.user_agent.c_str());
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
            if (!request.post_data.empty()) {
                std::string content_type = "Content-Type: " +
                    (request.content_type.empty() ? std::string("application/x-www-form-urlencoded") : request.content_type);
                headers = curl_slist_append(headers, content_type.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.post_data.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)request.post_data.size());
            }

            if (curl_easy_perform(curl) == CURLE_OK) {
                long status = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
                response.status = (int)status;
            }
            else {
                response.body.clear();
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }

        done(std::move(response));
    }).detach();
}

#endif
