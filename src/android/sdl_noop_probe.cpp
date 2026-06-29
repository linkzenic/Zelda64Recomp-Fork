#include <android/log.h>
#include <jni.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#include <chrono>
#include <thread>

namespace {
constexpr const char* kLogTag = "ZeldaSDLNoopProbe";
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetAndroidSurfaceReady(
    JNIEnv*,
    jclass,
    jboolean ready) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "surface ready=%d", ready ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetAppAudioActive(
    JNIEnv*,
    jclass,
    jboolean active) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "audio active=%d", active ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) jboolean Java_io_github_zelda64recomp_ZeldaSDLActivity_attachController(
    JNIEnv*,
    jobject) {
    return JNI_TRUE;
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_detachController(
    JNIEnv*,
    jobject) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_setButton(
    JNIEnv*,
    jobject,
    jint,
    jboolean) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_setAxis(
    JNIEnv*,
    jobject,
    jint,
    jshort) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_submitAndroidMotionSensor(
    JNIEnv*,
    jobject,
    jint,
    jfloat,
    jfloat,
    jfloat,
    jlong) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeOnFileDialogResult(
    JNIEnv*,
    jclass,
    jboolean,
    jstring) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeOnFileDialogMultipleResult(
    JNIEnv*,
    jclass,
    jboolean,
    jobjectArray) {
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetLogPaths(
    JNIEnv* env,
    jclass,
    jstring logPath,
    jstring crashPath) {
    const char* log = logPath ? env->GetStringUTFChars(logPath, nullptr) : nullptr;
    const char* crash = crashPath ? env->GetStringUTFChars(crashPath, nullptr) : nullptr;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "log paths log=%s crash=%s", log ? log : "(null)", crash ? crash : "(null)");
    if (log) {
        env->ReleaseStringUTFChars(logPath, log);
    }
    if (crash) {
        env->ReleaseStringUTFChars(crashPath, crash);
    }
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeReloadClockTexturePack(
    JNIEnv*,
    jclass) {
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL_main entered; calling SDL_Init(SDL_INIT_VIDEO)");

    SDL_version linked{};
    SDL_GetVersion(&linked);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL linked version %d.%d.%d", linked.major, linked.minor, linked.patch);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_Init(SDL_INIT_VIDEO) failed: %s", SDL_GetError());
        return 1;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL_Init(SDL_INIT_VIDEO) succeeded; video=%s",
                        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)");

    SDL_Window* window = SDL_CreateWindow("Zelda64 Recompiled SDL Window Probe",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          1280,
                                          720,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL window created");

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 3;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL renderer created");

    for (int i = 0; i < 7200; ++i) {
        if ((i % 60) == 0) {
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL_main alive tick=%d", i);
        }
        const Uint8 red = static_cast<Uint8>((i * 5) & 0xFF);
        const Uint8 green = static_cast<Uint8>((i * 3) & 0xFF);
        SDL_SetRenderDrawColor(renderer, red, green, 96, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL_main returning");
    return 0;
}
