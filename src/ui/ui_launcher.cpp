#include "recomp_ui.h"
#include "zelda_config.h"
#include "zelda_support.h"
#include "librecomp/game.hpp"
#include "ultramodern/ultramodern.hpp"
#include "RmlUi/Core.h"
#include "nfd.h"
#include <cstdlib>
#include <fstream>
#include <filesystem>

static std::string version_string;

Rml::DataModelHandle model_handle;
bool mm_rom_valid = false;
bool safe_mode_enabled = false;

extern std::vector<recomp::GameEntry> supported_games;

static bool android_safe_mode_from_env() {
#if defined(__ANDROID__)
    const char* safe_mode = std::getenv("APP_SAFE_MODE");
    return safe_mode != nullptr && safe_mode[0] == '1';
#else
    return false;
#endif
}

static std::filesystem::path android_safe_mode_flag_path() {
    const char* app_folder_path = std::getenv("APP_FOLDER_PATH");
    if (app_folder_path == nullptr || app_folder_path[0] == '\0') {
        return {};
    }

    return std::filesystem::path{ app_folder_path } / "Zelda64Recompiled_safe_mode.flag";
}

static void set_android_safe_mode(bool enabled) {
    safe_mode_enabled = enabled;
#if defined(__ANDROID__)
    setenv("APP_SAFE_MODE", enabled ? "1" : "0", 1);

    const std::filesystem::path flag_path = android_safe_mode_flag_path();
    if (!flag_path.empty()) {
        std::error_code ec;
        if (enabled) {
            std::filesystem::create_directories(flag_path.parent_path(), ec);
            std::ofstream flag_file{ flag_path };
            flag_file << "Safe mode enabled\n";
        }
        else {
            std::filesystem::remove(flag_path, ec);
        }
    }
#endif
    if (model_handle) {
        model_handle.DirtyVariable("safe_mode_enabled");
    }
}

void select_rom() {
    nfdnchar_t* native_path = nullptr;
    zelda64::open_file_dialog([](bool success, const std::filesystem::path& path) {
        if (success) {
            recomp::RomValidationError rom_error = recomp::select_rom(path, supported_games[0].game_id);
            switch (rom_error) {
                case recomp::RomValidationError::Good:
                    mm_rom_valid = true;
                    model_handle.DirtyVariable("mm_rom_valid");
                    break;
                case recomp::RomValidationError::FailedToOpen:
                    recompui::message_box("Failed to open ROM file.");
                    break;
                case recomp::RomValidationError::NotARom:
                    recompui::message_box("This is not a valid ROM file.");
                    break;
                case recomp::RomValidationError::IncorrectRom:
                    recompui::message_box("This ROM is not the correct game.");
                    break;
                case recomp::RomValidationError::NotYet:
                    recompui::message_box("This game isn't supported yet.");
                    break;
                case recomp::RomValidationError::IncorrectVersion:
                    recompui::message_box(
                            "This ROM is the correct game, but the wrong version.\nThis project requires the NTSC-U N64 version of the game.");
                    break;
                case recomp::RomValidationError::OtherError:
                    recompui::message_box("An unknown error has occurred.");
                    break;
            }
        }
    });
}

recompui::ContextId launcher_context;

recompui::ContextId recompui::get_launcher_context_id() {
	return launcher_context;
}

class LauncherMenu : public recompui::MenuController {
public:
    LauncherMenu() {
        mm_rom_valid = recomp::is_rom_valid(supported_games[0].game_id);
        safe_mode_enabled = android_safe_mode_from_env();
    }
    ~LauncherMenu() override {

    }
    void load_document() override {
		launcher_context = recompui::create_context(zelda64::get_asset_path("launcher.rml"));
    }
    void register_events(recompui::UiEventListenerInstancer& listener) override {
        recompui::register_event(listener, "select_rom",
            [](const std::string& param, Rml::Event& event) {
                select_rom();
            }
        );
        recompui::register_event(listener, "rom_selected",
            [](const std::string& param, Rml::Event& event) {
                mm_rom_valid = true;
                model_handle.DirtyVariable("mm_rom_valid");
            }
        );
        recompui::register_event(listener, "start_game",
            [](const std::string& param, Rml::Event& event) {
                recomp::start_game(supported_games[0].game_id);
                recompui::hide_all_contexts();
            }
        );
        recompui::register_event(listener, "toggle_safe_mode",
            [](const std::string& param, Rml::Event& event) {
                set_android_safe_mode(!safe_mode_enabled);
            }
        );
        recompui::register_event(listener, "open_controls",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::Controls);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_settings",
            [](const std::string& param, Rml::Event& event) {
                recompui::set_config_tab(recompui::ConfigTab::General);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "open_mods",
            [](const std::string &param, Rml::Event &event) {
                recompui::set_config_tab(recompui::ConfigTab::Mods);
                recompui::hide_all_contexts();
                recompui::show_context(recompui::get_config_context_id(), "");
            }
        );
        recompui::register_event(listener, "exit_game",
            [](const std::string& param, Rml::Event& event) {
                ultramodern::quit();
            }
        );
    }
    void make_bindings(Rml::Context* context) override {
        Rml::DataModelConstructor constructor = context->CreateDataModel("launcher_model");

        constructor.Bind("mm_rom_valid", &mm_rom_valid);
        constructor.Bind("safe_mode_enabled", &safe_mode_enabled);

        version_string = recomp::get_project_version().to_string();
#if defined(__ANDROID__)
        if (const char* android_version_name = std::getenv("APP_ANDROID_VERSION_NAME");
            android_version_name != nullptr && android_version_name[0] != '\0') {
            version_string += " Android ";
            version_string += android_version_name;
        }
#endif
        constructor.Bind("version_number", &version_string);

        model_handle = constructor.GetModelHandle();
    }
};

std::unique_ptr<recompui::MenuController> recompui::create_launcher_menu() {
    return std::make_unique<LauncherMenu>();
}
