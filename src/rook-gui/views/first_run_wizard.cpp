#include "first_run_wizard.hpp"

namespace rook::gui {

FirstRunWizard::FirstRunWizard()
    : Gtk::Window()
{
    set_title("Welcome to Rook");
    set_default_size(500, 400);
    set_modal(true);
    setupUi();
}

void FirstRunWizard::setupUi() {
    auto* main_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    auto* header = Gtk::make_managed<Gtk::HeaderBar>();
    header->set_title_widget(*Gtk::make_managed<Gtk::Label>("Setup"));
    main_box->append(*header);

    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 24);
    content->set_margin(48);
    content->set_valign(Gtk::Align::CENTER);
    content->set_halign(Gtk::Align::CENTER);

    auto* title = Gtk::make_managed<Gtk::Label>("Choose your LLM Provider");
    title->add_css_class("title-1");
    content->append(*title);

    auto* subtitle = Gtk::make_managed<Gtk::Label>(
        "Rook needs an LLM backend to function. You can change this later in Settings.");
    subtitle->set_wrap(true);
    subtitle->set_max_width_chars(40);
    subtitle->add_css_class("dim-label");
    content->append(*subtitle);

    m_provider.append("ollama", "Ollama (local, free)");
    m_provider.append("openai", "OpenAI (cloud, API key required)");
    m_provider.append("deepseek", "DeepSeek (cloud, API key required)");
    m_provider.append("anthropic", "Anthropic (cloud, API key required)");
    m_provider.set_active_id("ollama");
    m_provider.signal_changed().connect(
        sigc::mem_fun(*this, &FirstRunWizard::onProviderChanged));
    content->append(m_provider);

    m_model.set_placeholder_text("Model name");
    m_model.set_text("llama3.1");
    m_model.set_hexpand(true);
    content->append(m_model);

    m_api_key.set_placeholder_text("API Key");
    m_api_key.set_visibility(false);
    m_api_key.set_sensitive(false);
    content->append(m_api_key);

    auto* finish_btn = Gtk::make_managed<Gtk::Button>("Get Started");
    finish_btn->add_css_class("suggested-action");
    finish_btn->add_css_class("pill");
    finish_btn->signal_clicked().connect(
        sigc::mem_fun(*this, &FirstRunWizard::onFinish));
    content->append(*finish_btn);

    main_box->append(*content);
    set_child(*main_box);
}

void FirstRunWizard::onProviderChanged() {
    auto provider = m_provider.get_active_id();
    bool needs_key = (provider != "ollama");
    m_api_key.set_sensitive(needs_key);

    if (provider == "ollama") {
        m_model.set_text("llama3.1");
        m_api_key.set_text("");
    } else if (provider == "openai") {
        m_model.set_text("gpt-4o");
    } else if (provider == "deepseek") {
        m_model.set_text("deepseek-chat");
    } else if (provider == "anthropic") {
        m_model.set_text("claude-sonnet-4-20250514");
    }
}

void FirstRunWizard::onFinish() {
    m_signal_done();
    close();
}

rook::ports::LlmConfig FirstRunWizard::getConfig() const {
    rook::ports::LlmConfig config;
    config.provider = m_provider.get_active_id();
    config.model = m_model.get_text();
    config.api_key = m_api_key.get_text();
    return config;
}

FirstRunWizard::SlotDone FirstRunWizard::signal_done() {
    return m_signal_done;
}

} // namespace rook::gui
