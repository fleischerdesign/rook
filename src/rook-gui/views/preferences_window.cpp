#include "preferences_window.hpp"
#include "llm_settings_page.hpp"
#include "voice_settings_page.hpp"
#include "appearance_page.hpp"

namespace rook::gui {

PreferencesWindow::PreferencesWindow(Gtk::Window& parent,
                                     rook::ports::LlmPort& llm,
                                     const std::string& config_path)
    : m_llm(llm)
    , m_config_path(config_path)
{
    set_transient_for(parent);
    set_title("Preferences");
    set_default_size(720, 520);
    set_modal(true);

    setupUi();
}

void PreferencesWindow::setupUi() {
    auto* paned = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);

    m_sidebar.set_size_request(160, -1);
    m_sidebar.add_css_class("navigation-sidebar");
    m_sidebar.signal_row_activated().connect(
        sigc::mem_fun(*this, &PreferencesWindow::onSidebarRowActivated));

    auto header = Gtk::make_managed<Gtk::Label>("Preferences");
    header->set_xalign(0.0f);
    header->set_margin(12);
    header->add_css_class("title-4");

    auto sidebar_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    sidebar_box->append(*header);

    auto* scrolled = Gtk::make_managed<Gtk::ScrolledWindow>();
    scrolled->set_child(m_sidebar);
    sidebar_box->append(*scrolled);

    paned->set_start_child(*sidebar_box);

    m_llm_page = Gtk::make_managed<LlmSettingsPage>(m_llm);
    m_llm_page->signal_changed().connect(
        [this]() { m_signal_changed.emit(); });
    m_stack.add(*m_llm_page, "llm");

    m_voice_page = Gtk::make_managed<VoiceSettingsPage>();
    m_stack.add(*m_voice_page, "voice");

    m_appearance_page = Gtk::make_managed<AppearancePage>(m_config_path);
    m_stack.add(*m_appearance_page, "appearance");

    paned->set_end_child(m_stack);
    paned->set_position(160);
    paned->set_resize_start_child(false);
    paned->set_shrink_start_child(false);

    set_child(*paned);

    auto addSidebarEntry = [&](const char* label, const char* icon, const char* name) {
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        box->set_margin(6);
        auto* img = Gtk::make_managed<Gtk::Image>(icon);
        img->set_pixel_size(16);
        box->append(*img);
        auto* lbl = Gtk::make_managed<Gtk::Label>(label);
        lbl->set_xalign(0.0f);
        box->append(*lbl);
        row->set_child(*box);
        row->set_name(name);
        m_sidebar.append(*row);
        return row;
    };

    addSidebarEntry("LLM", "brain-augemind-symbolic", "llm");
    addSidebarEntry("Voice", "microphone-sensitivity-medium-symbolic", "voice");
    addSidebarEntry("Appearance", "preferences-desktop-theme-symbolic", "appearance");

    m_sidebar.select_row(*m_sidebar.get_row_at_index(0));
    m_stack.set_visible_child("llm");
}

void PreferencesWindow::onSidebarRowActivated(Gtk::ListBoxRow* row) {
    if (!row) return;
    auto name = row->get_name();
    m_stack.set_visible_child(name);
}

} // namespace rook::gui
