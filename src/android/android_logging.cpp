#include "android_logging.h"

#if defined(__ANDROID__)

#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <inttypes.h>
#include <jni.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unwind.h>

namespace {
constexpr const char* kLogTag = "ZeldaNative";

char log_path[512] = "/sdcard/Zelda64/Zelda64Recompiled.log";
char crash_path[512] = "/sdcard/Zelda64/Zelda64Recompiled_crash.txt";
char auto_safe_mode_path[512] = "/sdcard/Zelda64/Zelda64Recompiled_auto_safe_mode.flag";
char crash_stage[128] = "native startup";
bool crash_handlers_installed = false;

struct BacktraceState {
    uintptr_t frames[64];
    size_t count = 0;
};

void copy_path(char* destination, size_t destination_size, const char* source) {
    if (source == nullptr || source[0] == '\0' || destination_size == 0) {
        return;
    }

    std::strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

bool is_android_emulator() {
    char property_value[PROP_VALUE_MAX] = {};
    if (__system_property_get("ro.kernel.qemu", property_value) > 0 && std::strcmp(property_value, "1") == 0) {
        return true;
    }
    if (__system_property_get("ro.boot.qemu", property_value) > 0 && std::strcmp(property_value, "1") == 0) {
        return true;
    }
    if (__system_property_get("ro.hardware", property_value) > 0 && std::strcmp(property_value, "ranchu") == 0) {
        return true;
    }
    return false;
}

void write_all(int fd, const char* text) {
    if (text == nullptr) {
        return;
    }

    const char* cursor = text;
    size_t remaining = std::strlen(text);
    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written <= 0) {
            break;
        }
        cursor += written;
        remaining -= static_cast<size_t>(written);
    }
}

_Unwind_Reason_Code unwind_callback(_Unwind_Context* context, void* user_data) {
    BacktraceState* state = static_cast<BacktraceState*>(user_data);
    if (state->count >= (sizeof(state->frames) / sizeof(state->frames[0]))) {
        return _URC_END_OF_STACK;
    }

    uintptr_t pc = static_cast<uintptr_t>(_Unwind_GetIP(context));
    if (pc != 0) {
        state->frames[state->count++] = pc;
    }

    return _URC_NO_REASON;
}

const char* signal_name(int signal_number) {
    switch (signal_number) {
    case SIGABRT:
        return "SIGABRT";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    case SIGSEGV:
        return "SIGSEGV";
    default:
        return "unknown";
    }
}

void write_backtrace(int fd) {
    BacktraceState state;
    _Unwind_Backtrace(unwind_callback, &state);

    write_all(fd, "Backtrace:\n");
    if (state.count == 0) {
        write_all(fd, "  <unavailable>\n");
        return;
    }

    for (size_t i = 0; i < state.count; i++) {
        Dl_info info{};
        const bool found_symbol = dladdr(reinterpret_cast<void*>(state.frames[i]), &info) != 0;
        const uintptr_t library_offset = found_symbol && info.dli_fbase != nullptr
            ? state.frames[i] - reinterpret_cast<uintptr_t>(info.dli_fbase)
            : 0;

        char line[512];
        if (found_symbol) {
            std::snprintf(line, sizeof(line),
                "  #%02zu pc 0x%016" PRIxPTR " %s + 0x%" PRIxPTR " (%s)\n",
                i,
                state.frames[i],
                info.dli_fname != nullptr ? info.dli_fname : "<unknown>",
                library_offset,
                info.dli_sname != nullptr ? info.dli_sname : "<unknown>");
        }
        else {
            std::snprintf(line, sizeof(line),
                "  #%02zu pc 0x%016" PRIxPTR " <unknown>\n",
                i,
                state.frames[i]);
        }
        write_all(fd, line);
    }
}

void update_auto_safe_mode_path() {
    copy_path(auto_safe_mode_path, sizeof(auto_safe_mode_path), crash_path);
    char* slash = std::strrchr(auto_safe_mode_path, '/');
    if (slash == nullptr) {
        copy_path(auto_safe_mode_path, sizeof(auto_safe_mode_path), "/sdcard/Zelda64/Zelda64Recompiled_auto_safe_mode.flag");
        return;
    }

    std::snprintf(slash + 1,
        sizeof(auto_safe_mode_path) - static_cast<size_t>((slash + 1) - auto_safe_mode_path),
        "%s",
        "Zelda64Recompiled_auto_safe_mode.flag");
}

void write_auto_safe_mode_marker() {
    int fd = open(auto_safe_mode_path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Unable to write safe mode marker: %s", auto_safe_mode_path);
        return;
    }

    write_all(fd, "Safe mode will be enabled after this crash.\n");
    close(fd);
}

void write_crash_report(const char* message, siginfo_t* info) {
    write_auto_safe_mode_marker();

    int fd = open(crash_path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Unable to write crash file: %s", crash_path);
        return;
    }

    write_all(fd, "Zelda64 Recompiled Android crash\n");
    write_all(fd, message);
    write_all(fd, "\n");

    char line[256];
    std::snprintf(line, sizeof(line), "Stage: %s\n", crash_stage);
    write_all(fd, line);

    char thread_name[64] = {};
    prctl(PR_GET_NAME, thread_name);
    std::snprintf(line, sizeof(line), "Host thread tid: %ld\nHost thread name: %s\n", static_cast<long>(syscall(SYS_gettid)),
                  thread_name[0] != '\0' ? thread_name : "<unknown>");
    write_all(fd, line);

    if (info != nullptr) {
        std::snprintf(line, sizeof(line), "Signal code: %d\nFault address: %p\n", info->si_code, info->si_addr);
        write_all(fd, line);
    }

    write_backtrace(fd);
    close(fd);
}

void signal_handler(int signal_number, siginfo_t* info, void*) {
    char message[128];
    std::snprintf(message, sizeof(message), "Native crash signal %d (%s)", signal_number, signal_name(signal_number));
    write_crash_report(message, info);

    struct sigaction action{};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signal_number, &action, nullptr);
    raise(signal_number);
}

void terminate_handler() {
    const char* message = "Native terminate";
    try {
        std::exception_ptr exception = std::current_exception();
        if (exception) {
            std::rethrow_exception(exception);
        }
    } catch (const std::exception& exception) {
        zelda64::android::append_log("Native terminate: %s", exception.what());
        zelda64::android::write_crash_file(exception.what());
        _exit(EXIT_FAILURE);
    } catch (...) {
        message = "Native terminate: unknown exception";
    }

    zelda64::android::append_log("%s", message);
    zelda64::android::write_crash_file(message);
    _exit(EXIT_FAILURE);
}
}

namespace zelda64::android {
void append_log(const char* format, ...) {
    FILE* file = std::fopen(log_path, "a");
    if (file == nullptr) {
        return;
    }

    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}

void install_crash_handlers() {
    if (crash_handlers_installed) {
        return;
    }
    crash_handlers_installed = true;

    std::set_terminate(terminate_handler);

    struct sigaction action{};
    action.sa_sigaction = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGSEGV, &action, nullptr);
}

void redirect_stdio_to_log() {
    if (log_path[0] == '\0') {
        return;
    }

    if (is_android_emulator()) {
        append_log("Skipping stdout/stderr file redirection on Android emulator");
        return;
    }

    std::fflush(nullptr);
    FILE* stdout_file = std::freopen(log_path, "a", stdout);
    FILE* stderr_file = std::freopen(log_path, "a", stderr);
    if (stdout_file != nullptr) {
        setvbuf(stdout, nullptr, _IOLBF, 0);
    }
    if (stderr_file != nullptr) {
        setvbuf(stderr, nullptr, _IOLBF, 0);
    }
}

void set_crash_stage(const char* stage) {
    if (stage == nullptr || stage[0] == '\0') {
        return;
    }

    copy_path(crash_stage, sizeof(crash_stage), stage);
    append_log("Crash stage: %s", crash_stage);
}

void write_crash_file(const char* message) {
    write_crash_report(message, nullptr);
}
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetLogPaths(
    JNIEnv* env,
    jclass,
    jstring java_log_path,
    jstring java_crash_path) {
    const char* log_path_chars = java_log_path != nullptr ? env->GetStringUTFChars(java_log_path, nullptr) : nullptr;
    const char* crash_path_chars = java_crash_path != nullptr ? env->GetStringUTFChars(java_crash_path, nullptr) : nullptr;

    copy_path(log_path, sizeof(log_path), log_path_chars);
    copy_path(crash_path, sizeof(crash_path), crash_path_chars);
    update_auto_safe_mode_path();

    if (log_path_chars != nullptr) {
        env->ReleaseStringUTFChars(java_log_path, log_path_chars);
    }
    if (crash_path_chars != nullptr) {
        env->ReleaseStringUTFChars(java_crash_path, crash_path_chars);
    }

    zelda64::android::redirect_stdio_to_log();
    zelda64::android::install_crash_handlers();
    zelda64::android::append_log("Native log path ready");
}

#endif
