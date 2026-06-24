#ifndef RECOMPUI_SAVE_EDITOR_CONFIG_H
#define RECOMPUI_SAVE_EDITOR_CONFIG_H

#include "ui_config_sub_menu.h"

namespace recompui {

class SaveEditorConfigPanel : public Element {
public:
    SaveEditorConfigPanel(Element *parent, Rml::Element *host_element);
    virtual ~SaveEditorConfigPanel();
    void refresh();
protected:
    void process_event(const Event &e) override;
    std::string_view get_type_name() override { return "SaveEditorConfigPanel"; }
private:
    enum class Mode {
        None,
        PregameSetup,
        LiveEditor,
    };

    void rebuild_if_ready();
    void populate_setup_options();
    void populate_live_editor();
    void set_mode(Mode new_mode);

    ConfigSubMenu *config_sub_menu = nullptr;
    Rml::Element *host_element = nullptr;
    Mode mode = Mode::None;
};

class ElementSaveEditorConfig : public Rml::Element {
public:
    ElementSaveEditorConfig(const Rml::String &tag);
    virtual ~ElementSaveEditorConfig();
private:
    SaveEditorConfigPanel *panel = nullptr;
};

void refresh_save_editor_config();

}

#endif
