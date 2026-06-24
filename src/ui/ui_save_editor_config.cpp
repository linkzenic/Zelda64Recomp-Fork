#include "ui_save_editor_config.h"

#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#define SAVE_EDITOR_UI_LOG(...) __android_log_print(ANDROID_LOG_INFO, "ZeldaNative", __VA_ARGS__)
#else
#define SAVE_EDITOR_UI_LOG(...)
#endif

#include "recomp_ui.h"
#include "ultramodern/ultramodern.hpp"
#include "zelda_save_editor.h"

namespace recompui {

namespace {
    SaveEditorConfigPanel* active_save_editor_panel = nullptr;

    const std::vector<std::string> off_on_options{ "Off", "On" };
    const std::vector<std::string> target_file_options{ "Next opened file", "File 1", "File 2", "File 3" };
    const std::vector<std::string> preserve_day_options{ "Preserve", "Day 1", "Day 2", "Day 3", "Final Day" };
    const std::vector<std::string> preserve_bool_options{ "Preserve", "No", "Yes" };
    const std::vector<std::string> wallet_options{ "Child", "Adult", "Giant" };
    const std::vector<std::string> sword_options{ "None", "Kokiri", "Razor", "Gilded" };
    const std::vector<std::string> shield_options{ "None", "Hero", "Mirror" };
    const std::vector<std::string> magic_options{ "None", "Single", "Double" };
    const std::vector<std::string> quiver_options{ "None", "30", "40", "50" };
    const std::vector<std::string> bomb_bag_options{ "None", "20", "30", "40" };
    const std::vector<std::string> stick_upgrade_options{ "None", "10", "20", "30" };
    const std::vector<std::string> nut_upgrade_options{ "None", "20", "30", "40" };

    uint32_t setup_auto_apply = 0;
    uint32_t setup_target_file = 0;
    std::string setup_name;
    uint32_t setup_day = 0;
    uint32_t setup_has_tatl = 0;
    uint32_t setup_intro_complete = 0;
    uint32_t setup_owl_save = 0;

    int32_t value(zelda64::save_editor::ValueId id) {
        return zelda64::save_editor::get_pending_value(id);
    }

    void set_value(zelda64::save_editor::ValueId id, int32_t new_value) {
        zelda64::save_editor::set_pending_value(id, new_value);
    }

    int32_t raw_time_to_hour(int32_t raw_time) {
        return ((raw_time & 0xFFFF) * 24) / 0x10000;
    }

    int32_t hour_to_raw_time(int32_t hour) {
        return (hour % 24) * 0x10000 / 24;
    }
}

SaveEditorConfigPanel::SaveEditorConfigPanel(Element *parent, Rml::Element *host_element) : Element(parent, Events(EventType::Update)), host_element(host_element) {
    active_save_editor_panel = this;

    set_display(Display::Flex);
    set_flex(1, 1, 100.0f, Unit::Percent);
    set_height(100.0f, Unit::Percent);

    config_sub_menu = get_current_context().create_element<ConfigSubMenu>(this);
    config_sub_menu->set_header_visible(false);
    config_sub_menu->set_back_button_visible(false);
    config_sub_menu->enter("Save Editor");

    rebuild_if_ready();
}

SaveEditorConfigPanel::~SaveEditorConfigPanel() {
    if (active_save_editor_panel == this) {
        active_save_editor_panel = nullptr;
    }
}

void SaveEditorConfigPanel::refresh() {
    rebuild_if_ready();
}

void SaveEditorConfigPanel::process_event(const Event &e) {
    if (e.type == EventType::Update) {
        rebuild_if_ready();
    }
}

void SaveEditorConfigPanel::rebuild_if_ready() {
    Rml::ElementDocument* doc = host_element != nullptr ? host_element->GetOwnerDocument() : nullptr;
    Rml::Element* tabset_el = doc != nullptr ? doc->GetElementById("config_tabset") : nullptr;
    Rml::ElementTabSet* tabset = rmlui_dynamic_cast<Rml::ElementTabSet*>(tabset_el);
    bool editor_tab_active = tabset != nullptr && tabset->GetActiveTab() == recompui::config_tab_to_index(recompui::ConfigTab::Editor);
    if (!editor_tab_active) {
        set_mode(Mode::None);
        return;
    }

    Mode desired_mode = ultramodern::is_game_started() ? Mode::LiveEditor : Mode::PregameSetup;
    if (desired_mode == mode) {
        return;
    }

    set_mode(desired_mode);
}

void SaveEditorConfigPanel::set_mode(Mode new_mode) {
    if (new_mode == mode) {
        return;
    }

    mode = new_mode;
    config_sub_menu->set_display(Display::None);

    if (mode == Mode::None) {
        config_sub_menu->clear_options();
    }
    else if (mode == Mode::PregameSetup) {
        config_sub_menu->set_display(Display::Flex);
        populate_setup_options();
    }
    else if (mode == Mode::LiveEditor) {
        config_sub_menu->set_display(Display::Flex);
        populate_live_editor();
    }
}

void SaveEditorConfigPanel::populate_setup_options() {
    SAVE_EDITOR_UI_LOG("SaveEditor UI setup populate begin");
    config_sub_menu->clear_options();
    config_sub_menu->enter("Save Editor");

    config_sub_menu->add_section_header("Setup");
    config_sub_menu->add_radio_option("configure_auto_apply", "Apply Setup on Load",
        "Apply the setup values below when the chosen save file opens. Changes made during gameplay are applied live.",
        setup_auto_apply, off_on_options, [](const std::string&, uint32_t value) { setup_auto_apply = value; });
    config_sub_menu->add_radio_option("configure_target_file", "Setup Save File",
        "Choose which save file receives setup values. Next opened file applies to whichever save is loaded next.",
        setup_target_file, target_file_options, [](const std::string&, uint32_t value) { setup_target_file = value; });
    config_sub_menu->add_text_option("configure_name", "Setup Name",
        "Player name to apply. Leave blank to keep the current name.",
        setup_name, [](const std::string&, const std::string& value) { setup_name = value; });
    config_sub_menu->add_radio_option("configure_day", "Setup Day",
        "Day to apply. Preserve keeps the current day.",
        setup_day, preserve_day_options, [](const std::string&, uint32_t value) { setup_day = value; });
    config_sub_menu->add_radio_option("configure_has_tatl", "Setup Tatl",
        "Set whether the save has Tatl.",
        setup_has_tatl, preserve_bool_options, [](const std::string&, uint32_t value) { setup_has_tatl = value; });
    config_sub_menu->add_radio_option("configure_intro_complete", "Setup Intro Complete",
        "Set the intro-complete flag.",
        setup_intro_complete, preserve_bool_options, [](const std::string&, uint32_t value) { setup_intro_complete = value; });
    config_sub_menu->add_radio_option("configure_owl_save", "Setup Owl Save",
        "Set the owl-save flag.",
        setup_owl_save, preserve_bool_options, [](const std::string&, uint32_t value) { setup_owl_save = value; });

    SAVE_EDITOR_UI_LOG("SaveEditor UI setup populate end");
}

void refresh_save_editor_config() {
    if (active_save_editor_panel != nullptr) {
        recompui::ContextId ui_context = recompui::get_config_context_id();
        bool opened = ui_context.open_if_not_already();

        active_save_editor_panel->refresh();

        if (opened) {
            ui_context.close();
        }
    }
}

void SaveEditorConfigPanel::populate_live_editor() {
    using namespace zelda64::save_editor;

    SAVE_EDITOR_UI_LOG("SaveEditor UI populate begin");
    config_sub_menu->clear_options();
    config_sub_menu->enter("Save Editor");

    auto add_bool = [this](std::string_view id, std::string_view name, std::string_view description, ValueId value_id) {
        config_sub_menu->add_radio_option(id, name, description, value(value_id) != 0 ? 1 : 0, off_on_options,
            [value_id](const std::string &, uint32_t new_value) { set_value(value_id, static_cast<int32_t>(new_value)); });
    };

    auto add_slider = [this](std::string_view id, std::string_view name, std::string_view description, ValueId value_id,
                             double min, double max, double step) {
        config_sub_menu->add_slider_option(id, name, description, value(value_id), min, max, step, false,
            [value_id](const std::string &, double new_value) { set_value(value_id, static_cast<int32_t>(new_value)); });
    };

    auto add_radio = [this](std::string_view id, std::string_view name, std::string_view description, ValueId value_id,
                            const std::vector<std::string> &options) {
        config_sub_menu->add_radio_option(id, name, description, value(value_id), options,
            [value_id](const std::string &, uint32_t new_value) { set_value(value_id, static_cast<int32_t>(new_value)); });
    };

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Time");
    config_sub_menu->add_section_header("Time");
    config_sub_menu->add_slider_option("day", "Day", "Sets the current three-day cycle day.", value(Day), 1, 4, 1, false,
        [](const std::string &, double new_value) { set_value(Day, static_cast<int32_t>(new_value)); });
    config_sub_menu->add_slider_option("time_of_day", "Time of Day", "Sets the current hour of the day.", raw_time_to_hour(value(Time)), 0, 23, 1, false,
        [](const std::string &, double new_value) { set_value(Time, hour_to_raw_time(static_cast<int32_t>(new_value))); });
    config_sub_menu->add_slider_option("time_speed", "Time Speed", "Adjusts the current flow of time.", value(TimeSpeed), -2, 18, 1, false,
        [](const std::string &, double new_value) { set_value(TimeSpeed, static_cast<int32_t>(new_value)); });

    SAVE_EDITOR_UI_LOG("SaveEditor UI section State");
    config_sub_menu->add_section_header("State");
    add_bool("tatl", "Tatl", "Controls whether Tatl is with Link.", Tatl);
    add_bool("intro", "Intro Complete", "Controls whether the opening cycle has been completed.", IntroComplete);
    add_bool("owl_save", "Owl Save", "Controls whether the active save is marked as an owl save.", OwlSave);

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Health and Currency");
    config_sub_menu->add_section_header("Health and Currency");
    add_radio("wallet", "Wallet", "Sets the active wallet upgrade.", Wallet, wallet_options);
    add_slider("rupees", "Rupees", "Sets carried rupees.", Rupees, 0, 999, 1);
    add_slider("bank", "Bank Rupees", "Sets banked rupees.", BankRupees, 0, 5000, 1);
    add_slider("hearts", "Heart Containers", "Sets maximum heart containers.", Hearts, 3, 20, 1);
    add_slider("heart_pieces", "Heart Pieces", "Sets the current heart piece count.", HeartPieces, 0, 3, 1);
    add_slider("health", "Health", "Sets current health in raw quarter-heart units.", Health, 0, 320, 1);
    add_bool("double_defense", "Double Defense", "Controls double defense hearts.", DoubleDefense);
    add_radio("magic_level", "Magic Level", "Sets the active magic meter size.", MagicLevel, magic_options);
    add_slider("magic", "Magic", "Sets current magic.", Magic, 0, 96, 1);

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Equipment");
    config_sub_menu->add_section_header("Equipment");
    add_radio("sword", "Sword", "Sets the equipped sword upgrade.", Sword, sword_options);
    add_radio("shield", "Shield", "Sets the equipped shield upgrade.", Shield, shield_options);
    add_radio("quiver", "Quiver", "Sets the quiver upgrade.", Quiver, quiver_options);
    add_radio("bomb_bag", "Bomb Bag", "Sets the bomb bag upgrade.", BombBag, bomb_bag_options);
    add_radio("stick_upgrade", "Stick Upgrade", "Sets the Deku Stick upgrade.", StickUpgrade, stick_upgrade_options);
    add_radio("nut_upgrade", "Nut Upgrade", "Sets the Deku Nut upgrade.", NutUpgrade, nut_upgrade_options);

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Inventory");
    config_sub_menu->add_section_header("Inventory");
    add_slider("arrows", "Arrows", "Sets bow ammo and grants the bow if above zero.", Arrows, 0, 99, 1);
    add_slider("bombs", "Bombs", "Sets bomb ammo and grants bombs if above zero.", Bombs, 0, 99, 1);
    add_slider("bombchus", "Bombchus", "Sets bombchu ammo and grants bombchus if above zero.", Bombchus, 0, 99, 1);
    add_slider("sticks", "Deku Sticks", "Sets Deku Stick ammo and grants Deku Sticks if above zero.", DekuSticks, 0, 99, 1);
    add_slider("nuts", "Deku Nuts", "Sets Deku Nut ammo and grants Deku Nuts if above zero.", DekuNuts, 0, 99, 1);
    add_bool("powder_keg", "Powder Keg", "Controls whether the Powder Keg is in inventory.", PowderKeg);
    add_bool("ocarina", "Ocarina", "Controls whether the Ocarina of Time is in inventory.", Ocarina);
    add_bool("fire_arrows", "Fire Arrows", "Controls whether Fire Arrows are in inventory.", FireArrows);
    add_bool("ice_arrows", "Ice Arrows", "Controls whether Ice Arrows are in inventory.", IceArrows);
    add_bool("light_arrows", "Light Arrows", "Controls whether Light Arrows are in inventory.", LightArrows);
    add_bool("magic_beans", "Magic Beans", "Controls whether Magic Beans are in inventory.", MagicBeans);
    add_bool("pictograph_box", "Pictograph Box", "Controls whether the Pictograph Box is in inventory.", PictographBox);
    add_bool("lens", "Lens of Truth", "Controls whether the Lens of Truth is in inventory.", LensOfTruth);
    add_bool("hookshot", "Hookshot", "Controls whether the Hookshot is in inventory.", Hookshot);
    add_bool("great_fairy_sword", "Great Fairy Sword", "Controls whether the Great Fairy Sword is in inventory.", GreatFairySword);
    add_bool("room_key", "Room Key", "Controls whether the Room Key is in inventory.", RoomKey);
    add_bool("letter_mama", "Letter to Mama", "Controls whether Letter to Mama is in inventory.", LetterMama);
    add_bool("letter_kafei", "Letter to Kafei", "Controls whether Letter to Kafei is in inventory.", LetterKafei);
    add_bool("pendant", "Pendant of Memories", "Controls whether the Pendant of Memories is in inventory.", PendantOfMemories);

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Masks");
    config_sub_menu->add_section_header("Masks");
    add_bool("deku_mask", "Deku Mask", "Controls whether the Deku Mask is in inventory.", DekuMask);
    add_bool("goron_mask", "Goron Mask", "Controls whether the Goron Mask is in inventory.", GoronMask);
    add_bool("zora_mask", "Zora Mask", "Controls whether the Zora Mask is in inventory.", ZoraMask);
    add_bool("fierce_deity_mask", "Fierce Deity Mask", "Controls whether the Fierce Deity Mask is in inventory.", FierceDeityMask);
    add_bool("mask_truth", "Mask of Truth", "Controls whether the Mask of Truth is in inventory.", MaskOfTruth);
    add_bool("kafeis_mask", "Kafei's Mask", "Controls whether Kafei's Mask is in inventory.", KafeisMask);
    add_bool("all_night_mask", "All-Night Mask", "Controls whether the All-Night Mask is in inventory.", AllNightMask);
    add_bool("bunny_hood", "Bunny Hood", "Controls whether the Bunny Hood is in inventory.", BunnyHood);
    add_bool("keaton_mask", "Keaton Mask", "Controls whether the Keaton Mask is in inventory.", KeatonMask);
    add_bool("garo_mask", "Garo's Mask", "Controls whether Garo's Mask is in inventory.", GaroMask);
    add_bool("romani_mask", "Romani's Mask", "Controls whether Romani's Mask is in inventory.", RomaniMask);
    add_bool("circus_leader_mask", "Circus Leader's Mask", "Controls whether Circus Leader's Mask is in inventory.", CircusLeaderMask);
    add_bool("postman_hat", "Postman's Hat", "Controls whether the Postman's Hat is in inventory.", PostmanHat);
    add_bool("couples_mask", "Couple's Mask", "Controls whether the Couple's Mask is in inventory.", CouplesMask);
    add_bool("great_fairy_mask", "Great Fairy's Mask", "Controls whether Great Fairy's Mask is in inventory.", GreatFairyMask);
    add_bool("gibdo_mask", "Gibdo Mask", "Controls whether the Gibdo Mask is in inventory.", GibdoMask);
    add_bool("don_gero_mask", "Don Gero's Mask", "Controls whether Don Gero's Mask is in inventory.", DonGeroMask);
    add_bool("kamaro_mask", "Kamaro's Mask", "Controls whether Kamaro's Mask is in inventory.", KamaroMask);
    add_bool("captains_hat", "Captain's Hat", "Controls whether Captain's Hat is in inventory.", CaptainsHat);
    add_bool("stone_mask", "Stone Mask", "Controls whether the Stone Mask is in inventory.", StoneMask);
    add_bool("bremen_mask", "Bremen Mask", "Controls whether the Bremen Mask is in inventory.", BremenMask);
    add_bool("blast_mask", "Blast Mask", "Controls whether the Blast Mask is in inventory.", BlastMask);
    add_bool("mask_scents", "Mask of Scents", "Controls whether the Mask of Scents is in inventory.", MaskOfScents);
    add_bool("giants_mask", "Giant's Mask", "Controls whether the Giant's Mask is in inventory.", GiantsMask);

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Quest");
    config_sub_menu->add_section_header("Quest");
    add_bool("notebook", "Bombers Notebook", "Controls whether the Bombers Notebook is owned.", BombersNotebook);
    add_bool("odolwa", "Odolwa Remains", "Controls whether Odolwa's Remains are owned.", OdolwaRemains);
    add_bool("goht", "Goht Remains", "Controls whether Goht's Remains are owned.", GohtRemains);
    add_bool("gyorg", "Gyorg Remains", "Controls whether Gyorg's Remains are owned.", GyorgRemains);
    add_bool("twinmold", "Twinmold Remains", "Controls whether Twinmold's Remains are owned.", TwinmoldRemains);

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Songs");
    config_sub_menu->add_section_header("Songs");
    add_bool("sonata", "Sonata of Awakening", "Controls whether Sonata of Awakening is learned.", Sonata);
    add_bool("lullaby", "Goron Lullaby", "Controls whether Goron Lullaby is learned.", Lullaby);
    add_bool("bossa_nova", "New Wave Bossa Nova", "Controls whether New Wave Bossa Nova is learned.", BossaNova);
    add_bool("elegy", "Elegy of Emptiness", "Controls whether Elegy of Emptiness is learned.", Elegy);
    add_bool("oath", "Oath to Order", "Controls whether Oath to Order is learned.", Oath);
    add_bool("saria", "Saria's Song", "Controls whether Saria's Song is learned.", SariasSong);
    add_bool("time_song", "Song of Time", "Controls whether Song of Time is learned.", SongOfTime);
    add_bool("healing", "Song of Healing", "Controls whether Song of Healing is learned.", SongOfHealing);
    add_bool("epona", "Epona's Song", "Controls whether Epona's Song is learned.", EponasSong);
    add_bool("soaring", "Song of Soaring", "Controls whether Song of Soaring is learned.", SongOfSoaring);
    add_bool("storms", "Song of Storms", "Controls whether Song of Storms is learned.", SongOfStorms);
    add_bool("sun", "Sun's Song", "Controls whether Sun's Song is learned.", SunsSong);

    SAVE_EDITOR_UI_LOG("SaveEditor UI section Dungeons");
    config_sub_menu->add_section_header("Dungeons");
    add_bool("woodfall_map", "Woodfall Map", "Controls whether the Woodfall map is owned.", WoodfallMap);
    add_bool("woodfall_compass", "Woodfall Compass", "Controls whether the Woodfall compass is owned.", WoodfallCompass);
    add_bool("woodfall_boss_key", "Woodfall Boss Key", "Controls whether the Woodfall boss key is owned.", WoodfallBossKey);
    add_slider("woodfall_keys", "Woodfall Small Keys", "Sets Woodfall small keys.", WoodfallSmallKeys, 0, 1, 1);
    add_slider("woodfall_fairies", "Woodfall Stray Fairies", "Sets Woodfall stray fairies.", WoodfallStrayFairies, 0, 15, 1);
    add_bool("snowhead_map", "Snowhead Map", "Controls whether the Snowhead map is owned.", SnowheadMap);
    add_bool("snowhead_compass", "Snowhead Compass", "Controls whether the Snowhead compass is owned.", SnowheadCompass);
    add_bool("snowhead_boss_key", "Snowhead Boss Key", "Controls whether the Snowhead boss key is owned.", SnowheadBossKey);
    add_slider("snowhead_keys", "Snowhead Small Keys", "Sets Snowhead small keys.", SnowheadSmallKeys, 0, 3, 1);
    add_slider("snowhead_fairies", "Snowhead Stray Fairies", "Sets Snowhead stray fairies.", SnowheadStrayFairies, 0, 15, 1);
    add_bool("great_bay_map", "Great Bay Map", "Controls whether the Great Bay map is owned.", GreatBayMap);
    add_bool("great_bay_compass", "Great Bay Compass", "Controls whether the Great Bay compass is owned.", GreatBayCompass);
    add_bool("great_bay_boss_key", "Great Bay Boss Key", "Controls whether the Great Bay boss key is owned.", GreatBayBossKey);
    add_slider("great_bay_keys", "Great Bay Small Keys", "Sets Great Bay small keys.", GreatBaySmallKeys, 0, 1, 1);
    add_slider("great_bay_fairies", "Great Bay Stray Fairies", "Sets Great Bay stray fairies.", GreatBayStrayFairies, 0, 15, 1);
    add_bool("stone_tower_map", "Stone Tower Map", "Controls whether the Stone Tower map is owned.", StoneTowerMap);
    add_bool("stone_tower_compass", "Stone Tower Compass", "Controls whether the Stone Tower compass is owned.", StoneTowerCompass);
    add_bool("stone_tower_boss_key", "Stone Tower Boss Key", "Controls whether the Stone Tower boss key is owned.", StoneTowerBossKey);
    add_slider("stone_tower_keys", "Stone Tower Small Keys", "Sets Stone Tower small keys.", StoneTowerSmallKeys, 0, 4, 1);
    add_slider("stone_tower_fairies", "Stone Tower Stray Fairies", "Sets Stone Tower stray fairies.", StoneTowerStrayFairies, 0, 15, 1);
    SAVE_EDITOR_UI_LOG("SaveEditor UI populate end");
}

ElementSaveEditorConfig::ElementSaveEditorConfig(const Rml::String &tag) : Rml::Element(tag) {
    SetProperty(Rml::PropertyId::Display, Rml::Style::Display::Flex);
    SetProperty("width", "100%");
    SetProperty("height", "100%");

    recompui::Element this_compat(this);
    panel = recompui::get_current_context().create_element<SaveEditorConfigPanel>(&this_compat, this);
}

ElementSaveEditorConfig::~ElementSaveEditorConfig() {
}

}
