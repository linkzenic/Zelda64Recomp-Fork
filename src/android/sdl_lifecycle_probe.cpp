#include <android/log.h>
#include <jni.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>

namespace {
constexpr const char* kLogTag = "ZeldaSDLProbe";
int virtualJoystickId = -1;
SDL_Joystick* virtualJoystick = nullptr;
SDL_JoystickID virtualJoystickInstanceId = -1;
constexpr SDL_JoystickID kAndroidMotionInstanceId = -0x5A64;
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetAndroidSurfaceReady(
    JNIEnv*,
    jclass,
    jboolean ready) {
    __android_log_print(ANDROID_LOG_VERBOSE, kLogTag, "surface ready=%d", ready ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeSetAppAudioActive(
    JNIEnv*,
    jclass,
    jboolean active) {
    __android_log_print(ANDROID_LOG_VERBOSE, kLogTag, "audio active=%d", active ? 1 : 0);
}

extern "C" __attribute__((visibility("default"))) jboolean Java_io_github_zelda64recomp_ZeldaSDLActivity_attachController(
    JNIEnv*,
    jobject) {
    if (virtualJoystick != nullptr) {
        return JNI_TRUE;
    }
    if ((SDL_WasInit(SDL_INIT_JOYSTICK) & SDL_INIT_JOYSTICK) == 0) {
        return JNI_FALSE;
    }

    virtualJoystickId = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (virtualJoystickId < 0) {
        return JNI_FALSE;
    }

    virtualJoystick = SDL_JoystickOpen(virtualJoystickId);
    if (virtualJoystick == nullptr) {
        SDL_JoystickDetachVirtual(virtualJoystickId);
        virtualJoystickId = -1;
        virtualJoystickInstanceId = -1;
        return JNI_FALSE;
    }
    virtualJoystickInstanceId = SDL_JoystickInstanceID(virtualJoystick);
    return JNI_TRUE;
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_detachController(
    JNIEnv*,
    jobject) {
    if (virtualJoystick != nullptr) {
        SDL_JoystickClose(virtualJoystick);
        virtualJoystick = nullptr;
    }
    if (virtualJoystickId >= 0) {
        SDL_JoystickDetachVirtual(virtualJoystickId);
        virtualJoystickId = -1;
    }
    virtualJoystickInstanceId = -1;
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_setButton(
    JNIEnv*,
    jobject,
    jint button,
    jboolean value) {
    if (virtualJoystick != nullptr) {
        SDL_JoystickSetVirtualButton(virtualJoystick, button, value ? 1 : 0);
    }
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_setAxis(
    JNIEnv*,
    jobject,
    jint axis,
    jshort value) {
    if (virtualJoystick != nullptr) {
        SDL_JoystickSetVirtualAxis(virtualJoystick, axis, value);
    }
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_submitAndroidMotionSensor(
    JNIEnv*,
    jobject,
    jint sensorType,
    jfloat x,
    jfloat y,
    jfloat z,
    jlong timestampNs) {
    SDL_Event event{};
    event.type = SDL_CONTROLLERSENSORUPDATE;
    event.csensor.timestamp = static_cast<Uint32>(timestampNs / 1000000);
    event.csensor.which = virtualJoystickInstanceId >= 0 ? virtualJoystickInstanceId : kAndroidMotionInstanceId;
    event.csensor.sensor = sensorType == 0 ? SDL_SENSOR_ACCEL : SDL_SENSOR_GYRO;
    event.csensor.data[0] = x;
    event.csensor.data[1] = y;
    event.csensor.data[2] = z;
    event.csensor.timestamp_us = static_cast<Uint64>(timestampNs / 1000);
    SDL_PushEvent(&event);
}

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL_main entered");

    SDL_version linked{};
    SDL_GetVersion(&linked);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL linked version %d.%d.%d", linked.major, linked.minor, linked.patch);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL_Init succeeded; video=%s audio=%s",
                        SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)",
                        SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");

    SDL_Window* window = SDL_CreateWindow("Zelda64 Recompiled SDL Probe",
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
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "SDL_CreateRenderer failed: %s", SDL_GetError());
    }

    SDL_Event event;
    bool running = true;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL_QUIT received");
                running = false;
            }
        }

        if (renderer != nullptr) {
            const Uint32 ticks = SDL_GetTicks();
            const Uint8 red = static_cast<Uint8>((ticks / 8) % 255);
            const Uint8 green = static_cast<Uint8>((ticks / 13) % 255);
            const Uint8 blue = 96;
            SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
        }

        SDL_Delay(16);
    }

    if (renderer != nullptr) {
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL lifecycle probe completed");
    return 0;
}
