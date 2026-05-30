#include "settings_dialog.hpp"

namespace rook::gui {

SettingsDialog::SettingsDialog(Gtk::Window& parent)
    : Gtk::Dialog("Settings", parent, true)
{
    setupUi();
}

void SettingsDialog::setupUi() {
    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    content->set_margin(24);

    auto makeRow = [](auto& label, auto& widget) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto* lbl = Gtk::make_managed<Gtk::Label>(label);
        lbl->set_xalign(0.0f);
        lbl->set_width_chars(16);
        row->append(*lbl);
        widget.set_hexpand(true);
        row->append(widget);
        return row;
    };

    m_provider.append("ollama", "Ollama (local)");
    m_provider.append("openai", "OpenAI");
    m_provider.append("deepseek", "DeepSeek");
    m_provider.append("anthropic", "Anthropic");
    m_provider.set_active_id("ollama");
    m_provider.signal_changed().connect(
        sigc::mem_fun(*this, &SettingsDialog::onProviderChanged));

    content->append(*makeRow("Provider", m_provider));

    m_model.set_placeholder_text("e.g. llama3.1, gpt-4o, deepseek-chat");
    content->append(*makeRow("Model", m_model));

    m_api_key.set_placeholder_text("API key (not needed for Ollama)");
    m_api_key.set_visibility(false);
    content->append(*makeRow("API Key", m_api_key));

    m_system_prompt.set_placeholder_text("System prompt");
    content->append(*makeRow("System Prompt", m_system_prompt));

    m_temperature.set_range(0.0, 2.0);
    m_temperature.set_value(0.7);
    m_temperature.set_draw_value(true);
    m_temperature.set_hexpand(true);
    content->append(*makeRow("Temperature", m_temperature));

    get_content_area()->append(*content);

    add_button("Cancel", Gtk::ResponseType::CANCEL);
    add_button("Save", Gtk::ResponseType::OK);
    set_default_response(Gtk::ResponseType::OK);

    onProviderChanged();
}

void SettingsDialog::onProviderChanged() {
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

rook::ports::LlmConfig SettingsDialog::getConfig() const {
    rook::ports::LlmConfig config;
    config.provider = m_provider.get_active_id();
    config.model = m_model.get_text();
    config.api_key = m_api_key.get_text();
    config.system_prompt = m_system_prompt.get_text();
    config.temperature = static_cast<float>(m_temperature.get_value());
    config.max_tokens = 4096;
    return config;
}

void SettingsDialog::setConfig(const rook::ports::LlmConfig& config) {
    m_provider.set_active_id(config.provider);
    m_model.set_text(config.model);
    m_api_key.set_text(config.api_key);
    m_system_prompt.set_text(config.system_prompt);
    m_temperature.set_value(config.temperature);
}

} // namespace rook::gui
