#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <functional>
#include <memory>
#include "rook/ports/llm_port.hpp"

namespace rook::gui {

class LlmSettingsPage {
public:
    using ChangeFn = std::function<void()>;

    static std::unique_ptr<LlmSettingsPage> create(rook::ports::LlmPort &llm,
                                                     ChangeFn on_changed);
    void populate(peel::Adw::PreferencesGroup &group);

private:
    LlmSettingsPage() = default;

    rook::ports::LlmPort *m_llm = nullptr;
    peel::Gtk::ListBox *m_list = nullptr;
    ChangeFn m_on_changed;

    void refreshList();
    void onAddClicked();
    void onEditClicked(const std::string &provider_id);
    void onDeleteClicked(const std::string &provider_id);
    void onToggleEnabled(const std::string &provider_id, bool enabled);
};

} // namespace rook::gui
