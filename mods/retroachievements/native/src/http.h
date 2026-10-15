#pragma once

#include <functional>
#include <string>

namespace ra_http {

struct Request {
    std::string url;
    std::string post_data;     // empty: GET
    std::string content_type;
    std::string user_agent;
};

struct Response {
    int status = 0;            // HTTP status, or a negative RC_API_SERVER_RESPONSE_* value on failure
    std::string body;
};

// Called once at start-up on the game thread. Returns false if this platform can't make requests.
bool init(std::string& error);

// Whether requests go through the RA Bridge app (Android only).
bool using_bridge();

// Runs the request on a worker thread and calls `done` from that thread when it finishes.
void send_async(Request request, std::function<void(Response)> done);

}
