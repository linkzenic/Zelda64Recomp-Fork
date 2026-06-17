#include "android_logging.h"

#if defined(__ANDROID__)

#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <jni.h>
#include <signal.h>
#include <unistd.h>

namespace {
constexpr const char* kLogTag = "ZeldaNative";

char log_path[512] = "/sdcard/Zelda64/Zelda64Recompiled.log";
char crash_path[512] = "/sdcard/Zelda64/Zelda64Recompiled_crash.txt";
bool crash_handlers_installed = false;

void copy_path(char* destination, size_t destination_size, const char* source) {
    if (source == nullptr || source[0] == '\0' || destination_size == 0) {
        return;
    }

    std::strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
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

void signal_handler(int signal_number) {
    char message[128];
    std::snprintf(message, sizeof(message), "Native crash signal %d", signal_number);
    zelda64::android::write_crash_file(message);
    signal(signal_number, SIG_DFL);
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
    signal(SIGABRT, signal_handler);
    signal(SIGBUS, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGSEGV, signal_handler);
}

void redirect_stdio_to_log() {
    if (log_path[0] == '\0') {
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

void write_crash_file(const char* message) {
    int fd = open(crash_path, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Unable to write crash file: %s", crash_path);
        return;
    }

    write_all(fd, "Zelda64 Recompiled Android crash\n");
    write_all(fd, message);
    write_all(fd, "\n");
    close(fd);
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
