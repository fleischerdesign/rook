#pragma once

#include <gtkmm.h>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class LlmSettingsPage : public Gtk::Box {
public:
    explicit LlmSettingsPage(rook::ports::LlmPort& llm);
    ~LlmSettingsPage() override = default;

    sigc::signal<void()>& signal_changed() { return m_signal_changed; }

private:
    void setupUi();
    void refreshList();
    Gtk::Window* getParentWindow();
    void onAddClicked();
    void onEditClicked(const std::string& provider_id);
    void onDeleteClicked(const std::string& provider_id);
    void onSetDefault(const std::string& provider_id);
    void onToggleEnabled(const std::string& provider_id, bool enabled);

    rook::ports::LlmPort& m_llm;
    sigc::signal<void()> m_signal_changed;
    Gtk::ListBox m_list;
    Gtk::Button m_add_button;
};

} // namespace rook::gui
