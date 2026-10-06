// Glue between the Android app (android/app) and the native game.
//
// SDL's activity loads libmain.so and calls SDL_main on its own thread. Everything the
// Java side needs to tell the game goes through the JNI functions below.

#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <list>
#include <string>
#include <thread>

#include "util/file.h"

int banjo_main(int argc, char** argv);
void banjo_install_crash_report(const char* path);

extern "C" void plume_android_set_surface_ready(int ready);

namespace {
    constexpr const char* log_tag = "BanjoThor";

    // Sends everything the game prints to stdout/stderr to logcat, so `adb logcat -s BanjoThor`
    // shows the same output the desktop version prints to its console.
    void redirect_output_to_logcat() {
        static int pipe_fds[2] = { -1, -1 };
        if (pipe(pipe_fds) != 0) {
            return;
        }

        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);

        std::thread([]() {
            char buffer[1024];
            std::string line;
            ssize_t count;
            while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
                line.append(buffer, size_t(count));
                size_t newline;
                while ((newline = line.find('\n')) != std::string::npos) {
                    __android_log_write(ANDROID_LOG_INFO, log_tag, line.substr(0, newline).c_str());
                    line.erase(0, newline + 1);
                }
            }
        }).detach();
    }
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    redirect_output_to_logcat();
    banjo_install_crash_report(std::getenv("BANJO_CRASH_REPORT"));

    // The activity unpacks the game's assets (UI files, fonts, controller database) into this
    // folder and points us at it. The game looks for them relative to the working directory.
    if (const char* program_dir = std::getenv("BANJO_PROGRAM_DIR")) {
        std::error_code ec;
        std::filesystem::current_path(program_dir, ec);
        if (ec) {
            __android_log_print(ANDROID_LOG_ERROR, log_tag, "Can't enter the program folder %s: %s", program_dir, ec.message().c_str());
        }
    }

    return banjo_main(argc, argv);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_hydriostatic_banjothor_ThorActivity_nativeOnDocumentsPicked(JNIEnv* env, jclass, jobjectArray paths) {
    std::list<std::filesystem::path> picked;
    if (paths != nullptr) {
        const jsize count = env->GetArrayLength(paths);
        for (jsize i = 0; i < count; i++) {
            auto path = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
            if (path == nullptr) {
                continue;
            }

            if (const char* chars = env->GetStringUTFChars(path, nullptr)) {
                picked.emplace_back(chars);
                env->ReleaseStringUTFChars(path, chars);
            }

            env->DeleteLocalRef(path);
        }
    }

    recompui::file::complete_android_document_picker(!picked.empty(), picked);
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_hydriostatic_banjothor_ThorActivity_nativeSetSurfaceReady(JNIEnv*, jclass, jboolean ready) {
    plume_android_set_surface_ready(ready == JNI_TRUE ? 1 : 0);
}
