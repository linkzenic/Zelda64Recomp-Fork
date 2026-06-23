#include "zelda_support.h"
#include "recomp_ui.h"
#include <SDL.h>
#include <cstdlib>
#if !defined(__ANDROID__)
#include "nfd.h"
#else
#include <SDL_system.h>
#include <android/log.h>
#include <jni.h>
#include <mutex>
#include <utility>
#endif
#include "RmlUi/Core.h"

namespace zelda64 {
    // MARK: - Internal Helpers
#if defined(__ANDROID__)
    namespace {
        std::mutex file_dialog_mutex;
        std::function<void(bool, const std::filesystem::path&)> file_dialog_callback;
        std::function<void(bool, const std::list<std::filesystem::path>&)> file_dialog_multiple_callback;

        void finish_android_file_dialog(bool success, const std::filesystem::path& path) {
            std::function<void(bool, const std::filesystem::path&)> callback;
            {
                std::lock_guard lock(file_dialog_mutex);
                callback = std::move(file_dialog_callback);
                file_dialog_callback = {};
            }

            if (callback) {
                callback(success, path);
            }
        }

        bool finish_android_file_dialog_multiple(bool success, const std::list<std::filesystem::path>& paths) {
            std::function<void(bool, const std::list<std::filesystem::path>&)> callback;
            {
                std::lock_guard lock(file_dialog_mutex);
                callback = std::move(file_dialog_multiple_callback);
                file_dialog_multiple_callback = {};
            }

            if (callback) {
                callback(success, paths);
                return true;
            }
            return false;
        }

        bool launch_android_file_dialog(const char* method_name) {
            JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
            jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
            if (env == nullptr || activity == nullptr) {
                __android_log_print(ANDROID_LOG_WARN, "ZeldaFileDialog", "Unable to access SDL Android activity");
                if (activity != nullptr) {
                    env->DeleteLocalRef(activity);
                }
                return false;
            }

            jclass activity_class = env->GetObjectClass(activity);
            if (activity_class == nullptr) {
                __android_log_print(ANDROID_LOG_WARN, "ZeldaFileDialog", "Unable to resolve activity class");
                env->DeleteLocalRef(activity);
                return false;
            }

            jmethodID open_method = env->GetStaticMethodID(activity_class, method_name, "()V");
            if (open_method == nullptr) {
                __android_log_print(ANDROID_LOG_WARN, "ZeldaFileDialog", "Unable to resolve %s", method_name);
                env->DeleteLocalRef(activity_class);
                env->DeleteLocalRef(activity);
                return false;
            }

            env->CallStaticVoidMethod(activity_class, open_method);
            const bool ok = !env->ExceptionCheck();
            if (!ok) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }

            env->DeleteLocalRef(activity_class);
            env->DeleteLocalRef(activity);
            return ok;
        }
    }
#endif

#if !defined(__ANDROID__)
    void perform_file_dialog_operation(const std::function<void(bool, const std::filesystem::path&)>& callback) {
        nfdnchar_t* native_path = nullptr;
        nfdresult_t result = NFD_OpenDialogN(&native_path, nullptr, 0, nullptr);

        bool success = (result == NFD_OKAY);
        std::filesystem::path path;

        if (success) {
            path = std::filesystem::path{native_path};
            NFD_FreePathN(native_path);
        }

        callback(success, path);
    }

    void perform_file_dialog_operation_multiple(const std::function<void(bool, const std::list<std::filesystem::path>&)>& callback) {
        const nfdpathset_t* native_paths = nullptr;
        nfdresult_t result = NFD_OpenDialogMultipleN(&native_paths, nullptr, 0, nullptr);

        bool success = (result == NFD_OKAY);
        std::list<std::filesystem::path> paths;
        nfdpathsetsize_t count = 0;

        if (success) {
            NFD_PathSet_GetCount(native_paths, &count);
            for (nfdpathsetsize_t i = 0; i < count; i++) {
                nfdnchar_t* cur_path = nullptr;
                nfdresult_t cur_result = NFD_PathSet_GetPathN(native_paths, i, &cur_path);
                if (cur_result == NFD_OKAY) {
                    paths.emplace_back(std::filesystem::path{cur_path});
                }
            }
            NFD_PathSet_Free(native_paths);
        }

        callback(success, paths);
    }
#endif

    // MARK: - Public API

    std::filesystem::path get_program_path() {
#if defined(__ANDROID__)
        if (const char* program_path = std::getenv("APP_PROGRAM_PATH")) {
            return std::filesystem::path{program_path};
        }
        return "";
#else
#if defined(__APPLE__)
        return get_bundle_resource_directory();
#elif defined(__linux__) && defined(RECOMP_FLATPAK)
        return "/app/bin";
#else
        return "";
#endif
#endif
    }

    std::filesystem::path get_asset_path(const char* asset) {
        return get_program_path() / "assets" / asset;
    }

    void open_file_dialog(std::function<void(bool success, const std::filesystem::path& path)> callback) {
#ifdef __ANDROID__
        {
            std::lock_guard lock(file_dialog_mutex);
            if (file_dialog_callback) {
                callback(false, {});
                return;
            }
            file_dialog_callback = std::move(callback);
        }

        if (!launch_android_file_dialog("openFileDialog")) {
            finish_android_file_dialog(false, {});
        }
#elif defined(__APPLE__)
        dispatch_on_ui_thread([callback]() {
            perform_file_dialog_operation(callback);
        });
#else
        perform_file_dialog_operation(callback);
#endif
    }

    void open_file_dialog_multiple(std::function<void(bool success, const std::list<std::filesystem::path>& paths)> callback) {
#ifdef __ANDROID__
        {
            std::lock_guard lock(file_dialog_mutex);
            if (file_dialog_multiple_callback) {
                callback(false, {});
                return;
            }
            file_dialog_multiple_callback = std::move(callback);
        }

        if (!launch_android_file_dialog("openModFileDialog")) {
            finish_android_file_dialog_multiple(false, {});
        }
#elif defined(__APPLE__)
        dispatch_on_ui_thread([callback]() {
            perform_file_dialog_operation_multiple(callback);
        });
#else
        perform_file_dialog_operation_multiple(callback);
#endif
    }

    void open_driver_file_dialog() {
#ifdef __ANDROID__
        if (!launch_android_file_dialog("openDriverFileDialog")) {
            show_error_message_box("GPU driver", "Unable to open the Android file picker.");
        }
#else
        show_error_message_box("GPU driver", "Custom GPU drivers are only supported on Android.");
#endif
    }

    void clear_custom_driver() {
#ifdef __ANDROID__
        if (!launch_android_file_dialog("clearCustomDriver")) {
            show_error_message_box("GPU driver", "Unable to clear the custom GPU driver.");
        }
#else
        show_error_message_box("GPU driver", "Custom GPU drivers are only supported on Android.");
#endif
    }

    void open_clock_texture_file_dialog() {
#ifdef __ANDROID__
        if (!launch_android_file_dialog("openClockTextureFileDialog")) {
            show_error_message_box("Clock texture pack", "Unable to open the Android file picker.");
        }
#else
        show_error_message_box("Clock texture pack", "3DS clock texture pack import is only supported on Android.");
#endif
    }

    void show_error_message_box(const char *title, const char *message) {
#ifdef __APPLE__
    std::string title_copy(title);
    std::string message_copy(message);

    dispatch_on_ui_thread([title_copy, message_copy] {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title_copy.c_str(), message_copy.c_str(), nullptr);
    });
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
#endif
    }
}

#if defined(__ANDROID__)
extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeOnFileDialogResult(
    JNIEnv* env,
    jclass,
    jboolean success,
    jstring path) {
    const char* utf_path = path != nullptr ? env->GetStringUTFChars(path, nullptr) : nullptr;
    std::filesystem::path selected_path = utf_path != nullptr ? std::filesystem::path{utf_path} : std::filesystem::path{};
    if (utf_path != nullptr) {
        env->ReleaseStringUTFChars(path, utf_path);
    }

    zelda64::finish_android_file_dialog(success == JNI_TRUE, selected_path);
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeOnFileDialogMultipleResult(
    JNIEnv* env,
    jclass,
    jboolean success,
    jobjectArray paths) {
    std::list<std::filesystem::path> selected_paths;
    if (success == JNI_TRUE && paths != nullptr) {
        jsize count = env->GetArrayLength(paths);
        for (jsize i = 0; i < count; i++) {
            jstring path = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
            const char* utf_path = path != nullptr ? env->GetStringUTFChars(path, nullptr) : nullptr;
            if (utf_path != nullptr) {
                selected_paths.emplace_back(std::filesystem::path{utf_path});
                env->ReleaseStringUTFChars(path, utf_path);
            }
            if (path != nullptr) {
                env->DeleteLocalRef(path);
            }
        }
    }

    bool handled = zelda64::finish_android_file_dialog_multiple(success == JNI_TRUE && !selected_paths.empty(), selected_paths);
    if (handled) {
        SDL_setenv("APP_PENDING_MOD_PATHS", "", true);
    }
    else {
        __android_log_print(ANDROID_LOG_INFO, "ZeldaFileDialog", "No live mod picker callback; leaving pending mod paths for startup install");
    }
}

extern "C" __attribute__((visibility("default"))) void Java_io_github_zelda64recomp_ZeldaSDLActivity_nativeReloadClockTexturePack(
    JNIEnv*,
    jclass) {
    recompui::request_clock_texture_reload();
}
#endif
