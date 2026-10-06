// Writes a short report when the game crashes: which signal, on which thread, and the call
// stack with function names where they're known. The activity picks the report up on the next
// launch and saves it, together with the app's log, to Download/BanjoThor (see ThorActivity).
//
// This runs inside a signal handler, so it sticks to plain file writes and avoids allocating.

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <unwind.h>

#include <cstdio>
#include <cstring>

namespace {
    char report_path[512];
    struct sigaction previous_actions[NSIG];

    struct StackState {
        uintptr_t* frames;
        size_t count;
        size_t max;
    };

    _Unwind_Reason_Code collect_frame(_Unwind_Context* context, void* arg) {
        auto* state = static_cast<StackState*>(arg);
        uintptr_t pc = _Unwind_GetIP(context);
        if (pc != 0) {
            if (state->count == state->max) {
                return _URC_END_OF_STACK;
            }
            state->frames[state->count++] = pc;
        }
        return _URC_NO_REASON;
    }

    void write_text(int fd, const char* text) {
        if (text != nullptr) {
            ssize_t ignored = write(fd, text, strlen(text));
            (void)ignored;
        }
    }

    const char* signal_name(int sig) {
        switch (sig) {
            case SIGSEGV: return "SIGSEGV (bad memory access)";
            case SIGABRT: return "SIGABRT (abort)";
            case SIGBUS: return "SIGBUS (bad memory alignment)";
            case SIGILL: return "SIGILL (illegal instruction)";
            case SIGFPE: return "SIGFPE (arithmetic error)";
            default: return "unknown signal";
        }
    }

    void handle_crash(int sig, siginfo_t* info, void* ucontext) {
        int fd = open(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            char line[512];
            char thread_name[32] = "?";
            pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name));

            snprintf(line, sizeof(line), "Signal: %s, code %d, fault address %p\nThread: %s\n\nStack:\n",
                signal_name(sig), info != nullptr ? info->si_code : 0, info != nullptr ? info->si_addr : nullptr, thread_name);
            write_text(fd, line);

            uintptr_t frames[64];
            StackState state{ frames, 0, 64 };
            _Unwind_Backtrace(collect_frame, &state);

            for (size_t i = 0; i < state.count; i++) {
                Dl_info dl{};
                if (dladdr(reinterpret_cast<void*>(frames[i]), &dl) != 0 && dl.dli_fname != nullptr) {
                    const char* library = strrchr(dl.dli_fname, '/');
                    library = (library != nullptr) ? library + 1 : dl.dli_fname;
                    const uintptr_t offset = frames[i] - reinterpret_cast<uintptr_t>(dl.dli_fbase);
                    if (dl.dli_sname != nullptr) {
                        snprintf(line, sizeof(line), "#%02zu %s+0x%zx  %s+0x%zx\n", i, library, size_t(offset),
                            dl.dli_sname, size_t(frames[i] - reinterpret_cast<uintptr_t>(dl.dli_saddr)));
                    }
                    else {
                        snprintf(line, sizeof(line), "#%02zu %s+0x%zx\n", i, library, size_t(offset));
                    }
                }
                else {
                    snprintf(line, sizeof(line), "#%02zu 0x%zx\n", i, size_t(frames[i]));
                }
                write_text(fd, line);
            }

            close(fd);
        }

        __android_log_print(ANDROID_LOG_FATAL, "BanjoThor", "Crashed with %s; report written to %s", signal_name(sig), report_path);

        // Hand the signal on to the previous handler (Android's own crash reporting) and let the
        // process die the normal way.
        const struct sigaction& previous = previous_actions[sig];
        sigaction(sig, &previous, nullptr);
        if ((previous.sa_flags & SA_SIGINFO) && previous.sa_sigaction != nullptr) {
            previous.sa_sigaction(sig, info, ucontext);
        }
        else if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != nullptr) {
            previous.sa_handler(sig);
        }
        raise(sig);
    }
}

void banjo_install_crash_report(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }

    snprintf(report_path, sizeof(report_path), "%s", path);

    // Handlers run on their own stack so a stack overflow can still be reported.
    static char alternate_stack[64 * 1024];
    stack_t stack{};
    stack.ss_sp = alternate_stack;
    stack.ss_size = sizeof(alternate_stack);
    sigaltstack(&stack, nullptr);

    struct sigaction action{};
    action.sa_sigaction = handle_crash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);

    const int signals[] = { SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE };
    for (int sig : signals) {
        sigaction(sig, &action, &previous_actions[sig]);
    }
}
