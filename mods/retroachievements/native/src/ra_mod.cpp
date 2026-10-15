// RetroAchievements for Banjo: Recompiled — native half of the mod.
//
// The mod loader loads this library next to bk_retroachievements.nrm and the mod's game-side code
// calls the three exports below. Each export has the recomp calling convention: arguments arrive
// in the MIPS registers of `ctx`, and pointers point into the game's RDRAM.
//
//   ra_init(username, password, hardcore, mods_folder)   once, on the first game frame
//   ra_frame()                                            every game frame
//   ra_toast_tick(title, title_size, body, body_size)     every game frame, drives the toasts
//
// Everything rcheevos does (login, game lookup, evaluating achievements) happens on the game
// thread inside these calls. HTTP requests run on worker threads, and their results are queued
// and handed back to rcheevos at the next ra_frame().

#include "http.h"

#include <rc_client.h>
#include <rc_consoles.h>
#include <rc_error.h>

#include "recomp.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#define RA_EXPORT extern "C" __attribute__((visibility("default")))

#ifndef RA_MOD_VERSION
#define RA_MOD_VERSION "0.0.0"
#endif

// Tells the mod loader which version of the native library API this was built against.
extern "C" {
__attribute__((visibility("default"))) uint32_t recomp_api_version = 1;
}

namespace {

// RetroAchievements maps the N64's 8 MB of RDRAM (4 MB + expansion pak) to addresses 0-0x7FFFFF.
constexpr uint32_t rdram_size = 0x800000;

// The ROM the game was started from, as stored by the recomp runtime in its app folder,
// one level above the mods folder.
constexpr const char* stored_rom_name = "bk.n64.us.1.0.z64";
constexpr const char* login_file_name = "bk_retroachievements_login.txt";

constexpr auto toast_duration = std::chrono::milliseconds(5000);
constexpr auto toast_gap = std::chrono::milliseconds(300);
constexpr size_t max_queued_toasts = 32;

rc_client_t* client = nullptr;
uint8_t* game_rdram = nullptr;
bool online = false;
bool started = false;

std::string user_agent;
std::string mods_folder;
std::string config_username;
std::string config_password;

// ---------------------------------------------------------------------------------------------
// Logging

void log_line(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
#if defined(__ANDROID__)
    __android_log_write(ANDROID_LOG_INFO, "BanjoRA", buffer);
#else
    fprintf(stderr, "[RetroAchievements] %s\n", buffer);
#endif
}

void rc_log(const char* message, const rc_client_t*) {
    log_line("rcheevos: %s", message);
}

// ---------------------------------------------------------------------------------------------
// Toasts. Queued from rcheevos events, shown one at a time by the game-side code.

struct Toast {
    std::string title;
    std::string body;
};

std::mutex toast_mutex;
std::deque<Toast> toasts;
bool toast_visible = false;
std::chrono::steady_clock::time_point toast_changed_at;

void push_toast(std::string title, std::string body = {}) {
    log_line("%s%s%s", title.c_str(), body.empty() ? "" : " - ", body.c_str());
    std::lock_guard lock(toast_mutex);
    if (toasts.size() >= max_queued_toasts) {
        toasts.pop_front();
    }
    toasts.push_back({ std::move(title), std::move(body) });
}

// ---------------------------------------------------------------------------------------------
// Work handed back to the game thread (HTTP responses).

std::mutex pending_mutex;
std::vector<std::function<void()>> pending;

void run_on_game_thread(std::function<void()> fn) {
    std::lock_guard lock(pending_mutex);
    pending.push_back(std::move(fn));
}

void run_pending() {
    std::vector<std::function<void()>> work;
    {
        std::lock_guard lock(pending_mutex);
        work.swap(pending);
    }
    for (auto& fn : work) {
        fn();
    }
}

// ---------------------------------------------------------------------------------------------
// Access to the game's memory and the mod's arguments.

std::string read_mips_string(uint8_t* rdram, gpr address) {
    std::string out;
    if ((uint32_t)address == 0) {
        return out;
    }
    for (size_t i = 0; i < 4096; i++) {
        char c = (char)MEM_B(i, address);
        if (c == '\0') {
            break;
        }
        out += c;
    }
    return out;
}

// Writes a NUL-terminated string into a buffer in game memory, cutting it at a UTF-8 boundary.
void write_mips_string(uint8_t* rdram, gpr address, uint32_t size, const std::string& value) {
    if ((uint32_t)address == 0 || size == 0) {
        return;
    }
    size_t length = std::min<size_t>(value.size(), size - 1);
    while (length > 0 && length < value.size() && (static_cast<uint8_t>(value[length]) & 0xC0) == 0x80) {
        length--;
    }
    for (size_t i = 0; i < length; i++) {
        MEM_B(i, address) = (int8_t)value[i];
    }
    MEM_B(length, address) = 0;
}

// rcheevos reads N64 memory in the layout emulators keep it in: RDRAM as host-endian 32-bit words.
// The recomp runtime keeps RDRAM in that same layout, so achievement definitions written for
// emulators read the same values here with a plain copy.
uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t*) {
    if (game_rdram == nullptr || address >= rdram_size) {
        return 0;
    }
    uint32_t count = std::min(num_bytes, rdram_size - address);
    memcpy(buffer, game_rdram + address, count);
    return count;
}

// ---------------------------------------------------------------------------------------------
// Server calls

void server_call(const rc_api_request_t* request, rc_client_server_callback_t callback, void* callback_data, rc_client_t*) {
    if (!online) {
        rc_api_server_response_t response{};
        response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        callback(&response, callback_data);
        return;
    }

    ra_http::Request http_request;
    http_request.url = request->url ? request->url : "";
    http_request.post_data = request->post_data ? request->post_data : "";
    http_request.content_type = request->content_type ? request->content_type : "";
    http_request.user_agent = user_agent;

    ra_http::send_async(std::move(http_request), [callback, callback_data](ra_http::Response http_response) {
        run_on_game_thread([callback, callback_data, http_response = std::move(http_response)]() {
            rc_api_server_response_t response{};
            response.body = http_response.body.c_str();
            response.body_length = http_response.body.size();
            response.http_status_code = http_response.status;
            callback(&response, callback_data);
        });
    });
}

// ---------------------------------------------------------------------------------------------
// Saved login token

std::string login_file_path() {
    return mods_folder + "/" + login_file_name;
}

bool load_token(std::string& username, std::string& token) {
    std::ifstream file(login_file_path());
    return file && std::getline(file, username) && std::getline(file, token) && !username.empty() && !token.empty();
}

void save_token(const char* username, const char* token) {
    std::ofstream file(login_file_path(), std::ios::trunc);
    if (file) {
        file << username << "\n" << token << "\n";
    }
}

void forget_token() {
    std::remove(login_file_path().c_str());
}

bool same_user(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](char x, char y) { return tolower((unsigned char)x) == tolower((unsigned char)y); });
}

// ---------------------------------------------------------------------------------------------
// Login and game load

void load_game();

void on_game_loaded(int result, const char* error_message, rc_client_t* c, void*) {
    if (result != RC_OK) {
        if (result == RC_NO_GAME_LOADED) {
            push_toast("RetroAchievements", "This ROM isn't recognized by RetroAchievements.");
        }
        else {
            push_toast("RetroAchievements: couldn't load the game", error_message ? error_message : rc_error_str(result));
        }
        return;
    }

    const rc_client_game_t* game = rc_client_get_game_info(c);
    rc_client_user_game_summary_t summary{};
    rc_client_get_user_game_summary(c, &summary);

    char body[256];
    if (summary.num_core_achievements == 0) {
        snprintf(body, sizeof(body), "No achievements yet for this game.");
    }
    else {
        snprintf(body, sizeof(body), "%u of %u achievements unlocked (%s)",
            summary.num_unlocked_achievements, summary.num_core_achievements,
            rc_client_get_hardcore_enabled(c) ? "hardcore" : "softcore");
    }
    push_toast(game && game->title ? game->title : "RetroAchievements", body);
}

void load_game() {
    std::string rom_path = mods_folder + "/../" + stored_rom_name;
    std::ifstream rom(rom_path, std::ios::binary);
    if (!rom) {
        push_toast("RetroAchievements", "Couldn't find the game's ROM to identify it.");
        log_line("ROM not found at %s", rom_path.c_str());
        return;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(rom)), std::istreambuf_iterator<char>());
    rc_client_begin_identify_and_load_game(client, RC_CONSOLE_NINTENDO_64, rom_path.c_str(),
        data.data(), data.size(), on_game_loaded, nullptr);
}

void login_with_password();

void on_logged_in(int result, const char* error_message, rc_client_t* c, void* userdata) {
    bool used_token = userdata != nullptr;

    if (result != RC_OK) {
        if (used_token && (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN || result == RC_ACCESS_DENIED)) {
            forget_token();
            if (!config_password.empty()) {
                login_with_password();
                return;
            }
            push_toast("RetroAchievements: please log in again", "Enter your password in the mod's settings.");
            return;
        }
        push_toast("RetroAchievements: login failed", error_message ? error_message : rc_error_str(result));
        return;
    }

    const rc_client_user_t* user = rc_client_get_user_info(c);
    if (user != nullptr) {
        save_token(user->username, user->token);
        char body[160];
        snprintf(body, sizeof(body), "Logged in as %s (%u points)", user->display_name,
            rc_client_get_hardcore_enabled(c) ? user->score : user->score_softcore);
        push_toast("RetroAchievements", body);
    }

    load_game();
}

void login_with_password() {
    rc_client_begin_login_with_password(client, config_username.c_str(), config_password.c_str(), on_logged_in, nullptr);
}

void login() {
    std::string saved_username, token;
    if (load_token(saved_username, token) && (config_username.empty() || same_user(saved_username, config_username))) {
        static int token_marker;
        rc_client_begin_login_with_token(client, saved_username.c_str(), token.c_str(), on_logged_in, &token_marker);
        return;
    }
    if (config_username.empty() || config_password.empty()) {
        push_toast("RetroAchievements", "Set your username and password in the mod's settings to earn achievements.");
        return;
    }
    login_with_password();
}

// ---------------------------------------------------------------------------------------------
// Events from rcheevos

void on_event(const rc_client_event_t* event, rc_client_t*) {
    char body[384];
    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
            const rc_client_achievement_t* a = event->achievement;
            snprintf(body, sizeof(body), "%s (%u points)", a->description ? a->description : "", a->points);
            push_toast(std::string("Achievement unlocked: ") + (a->title ? a->title : ""), body);
            break;
        }
        case RC_CLIENT_EVENT_GAME_COMPLETED:
            push_toast("Game complete!", "You've unlocked every achievement in Banjo-Kazooie.");
            break;
        case RC_CLIENT_EVENT_SUBSET_COMPLETED:
            push_toast("Subset complete!", event->subset && event->subset->title ? event->subset->title : "");
            break;
        case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
            push_toast("Leaderboard attempt started", event->leaderboard->title ? event->leaderboard->title : "");
            break;
        case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
            push_toast("Leaderboard attempt failed", event->leaderboard->title ? event->leaderboard->title : "");
            break;
        case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
            snprintf(body, sizeof(body), "%s: %s", event->leaderboard->title ? event->leaderboard->title : "",
                event->leaderboard->tracker_value ? event->leaderboard->tracker_value : "");
            push_toast("Leaderboard submitted", body);
            break;
        case RC_CLIENT_EVENT_SERVER_ERROR:
            push_toast("RetroAchievements error",
                event->server_error && event->server_error->error_message ? event->server_error->error_message : "Unknown error");
            break;
        case RC_CLIENT_EVENT_DISCONNECTED:
            push_toast("RetroAchievements: connection lost", "Unlocks will be sent when the connection is back.");
            break;
        case RC_CLIENT_EVENT_RECONNECTED:
            push_toast("RetroAchievements: reconnected", "Pending unlocks were sent.");
            break;
        default:
            break;
    }
}

}

// ---------------------------------------------------------------------------------------------
// Exports called by the mod's game-side code

// s32 ra_init(const char* username, const char* password, u32 hardcore, const char* mods_folder)
RA_EXPORT void ra_init(uint8_t* rdram, recomp_context* ctx) {
    game_rdram = rdram;
    if (started) {
        ctx->r2 = 0;
        return;
    }
    started = true;

    config_username = read_mips_string(rdram, ctx->r4);
    config_password = read_mips_string(rdram, ctx->r5);
    bool hardcore = (uint32_t)ctx->r6 != 0;
    mods_folder = read_mips_string(rdram, ctx->r7);

    client = rc_client_create(read_memory, server_call);
    if (client == nullptr) {
        push_toast("RetroAchievements", "Couldn't start (out of memory).");
        ctx->r2 = (gpr)(int32_t)-1;
        return;
    }
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_INFO, rc_log);
#if !defined(__ANDROID__)
    // Lets desktop test builds talk to a local mock server.
    if (const char* test_host = getenv("RA_TEST_HOST")) {
        rc_client_set_host(client, test_host);
    }
#endif
    rc_client_set_event_handler(client, on_event);
    rc_client_set_hardcore_enabled(client, hardcore ? 1 : 0);

    char clause[64] = {};
    rc_client_get_user_agent_clause(client, clause, sizeof(clause));
#if defined(__ANDROID__)
    const char* platform = "Android";
#elif defined(_WIN32)
    const char* platform = "Windows";
#elif defined(__APPLE__)
    const char* platform = "macOS";
#else
    const char* platform = "Linux";
#endif
    user_agent = std::string("BanjoRecompRetroAchievements/") + RA_MOD_VERSION + " (" + platform + ") " + clause;
    log_line("starting, user agent: %s", user_agent.c_str());

    std::string error;
    online = ra_http::init(error);
    if (!online) {
        log_line("offline: %s", error.c_str());
        push_toast("RetroAchievements is offline", "Can't connect: " + error + ".");
        ctx->r2 = (gpr)(int32_t)-1;
        return;
    }

    log_line("connected %s", ra_http::using_bridge() ? "through RA Bridge" : "directly");
    login();
    ctx->r2 = 0;
}

// void ra_frame(void)
RA_EXPORT void ra_frame(uint8_t* rdram, recomp_context*) {
    game_rdram = rdram;
    if (client == nullptr) {
        return;
    }
    run_pending();
    if (rc_client_is_game_loaded(client)) {
        rc_client_do_frame(client);
    }
    else {
        rc_client_idle(client);
    }
}

// s32 ra_toast_tick(char* title, u32 title_size, char* body, u32 body_size)
// Returns 0 when nothing changes, 1 when a new toast was written to the buffers, 2 to hide it.
RA_EXPORT void ra_toast_tick(uint8_t* rdram, recomp_context* ctx) {
    auto now = std::chrono::steady_clock::now();
    int32_t action = 0;

    std::lock_guard lock(toast_mutex);
    if (toast_visible) {
        if (now - toast_changed_at >= toast_duration) {
            toast_visible = false;
            toast_changed_at = now;
            action = 2;
        }
    }
    else if (!toasts.empty() && now - toast_changed_at >= toast_gap) {
        Toast toast = std::move(toasts.front());
        toasts.pop_front();
        write_mips_string(rdram, ctx->r4, (uint32_t)ctx->r5, toast.title);
        write_mips_string(rdram, ctx->r6, (uint32_t)ctx->r7, toast.body);
        toast_visible = true;
        toast_changed_at = now;
        action = 1;
    }

    ctx->r2 = (gpr)action;
}
