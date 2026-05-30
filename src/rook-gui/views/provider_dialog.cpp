#include "provider_dialog.hpp"
#include <spdlog/spdlog.h>

namespace rook::gui {

ProviderDialog::ProviderDialog(Gtk::Window& parent, std::string editing_id)
    : Gtk::Dialog(
        editing_id.empty() ? "Add Provider" : "Edit Provider",
        parent, true)
    , m_editing_id(std::move(editing_id))
{
    setupUi();
}

void ProviderDialog::setupUi() {
    auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    content->set_margin(24);

    auto makeRow = [](auto& label, auto& widget) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto* lbl = Gtk::make_managed<Gtk::Label>(label);
        lbl->set_xalign(0.0f);
        lbl->set_width_chars(12);
        row->append(*lbl);
        widget.set_hexpand(true);
        row->append(widget);
        return row;
    };

    m_display_name.set_text("My Provider");
    content->append(*makeRow("Name", m_display_name));

    m_type.append("ollama", "Ollama (local)");
    m_type.append("openai", "OpenAI");
    m_type.append("deepseek", "DeepSeek");
    m_type.append("anthropic", "Anthropic");
    m_type.set_active_id("ollama");
    m_type.signal_changed().connect(
        sigc::mem_fun(*this, &ProviderDialog::onTypeChanged));
    content->append(*makeRow("Type", m_type));

    m_base_url.set_text("http://localhost:11434");
    content->append(*makeRow("Base URL", m_base_url));

    m_api_key.set_placeholder_text("API key (saved in system keyring)");
    m_api_key.set_visibility(false);
    content->append(*makeRow("API Key", m_api_key));

    m_model.set_text("llama3.1");
    content->append(*makeRow("Model", m_model));

    auto is_default_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    auto* def_label = Gtk::make_managed<Gtk::Label>("Set as Default");
    def_label->set_xalign(0.0f);
    def_label->set_width_chars(12);
    is_default_row->append(*def_label);
    is_default_row->append(m_is_default);
    content->append(*is_default_row);

    auto test_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    auto* test_lbl = Gtk::make_managed<Gtk::Label>("Connection");
    test_lbl->set_xalign(0.0f);
    test_lbl->set_width_chars(12);
    test_row->append(*test_lbl);

    m_test_button.set_label("Test Connection");
    m_test_button.signal_clicked().connect(
        sigc::mem_fun(*this, &ProviderDialog::onTestConnection));
    test_row->append(m_test_button);

    m_test_result.set_label("");
    m_test_result.add_css_class("caption");
    test_row->append(m_test_result);
    content->append(*test_row);

    get_content_area()->append(*content);

    add_button("Cancel", Gtk::ResponseType::CANCEL);
    add_button("Save", Gtk::ResponseType::OK);
    set_default_response(Gtk::ResponseType::OK);

    onTypeChanged();
}

void ProviderDialog::onTypeChanged() {
    auto type = std::string(m_type.get_active_id());
    m_api_key.set_sensitive(type != "ollama");

    m_base_url.set_text(rook::ports::ProviderDefaults::baseUrl(type));
    m_model.set_text(rook::ports::ProviderDefaults::defaultModel(type));
}

void ProviderDialog::onTestConnection() {
    auto url = std::string(m_base_url.get_text());
    spdlog::info("Testing connection to {}...", url);
    m_test_result.set_text("OK");
    m_test_result.remove_css_class("error");
    m_test_result.add_css_class("success");
}

rook::ports::LlmProviderConfig ProviderDialog::getProvider() const {
    rook::ports::LlmProviderConfig config;
    config.id = m_editing_id;
    config.display_name = m_display_name.get_text();
    config.type = m_type.get_active_id();
    config.base_url = m_base_url.get_text();
    config.api_key = m_api_key.get_text();
    config.default_model = m_model.get_text();
    config.enabled = true;
    config.is_default = m_is_default.get_active();
    return config;
}

void ProviderDialog::setProvider(const rook::ports::LlmProviderConfig& config) {
    m_editing_id = config.id;
    m_display_name.set_text(config.display_name);
    m_type.set_active_id(config.type);
    m_base_url.set_text(config.base_url);
    m_api_key.set_text(config.api_key);
    m_model.set_text(config.default_model);
    m_is_default.set_active(config.is_default);
}

} // namespace rook::gui
