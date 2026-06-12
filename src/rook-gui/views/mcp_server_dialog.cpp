#include <glib/gi18n.h>
#include "mcp_server_dialog.hpp"
#include "rook/adapters/mcp/stdio_transport.hpp"
#include "rook/adapters/mcp/http_sse_transport.hpp"
#include "rook/adapters/mcp/mcp_client.hpp"
#include <peel/GLib/functions.h>
#include <spdlog/spdlog.h>
#include <future>
#include <chrono>

using namespace peel;

namespace rook::gui {

Signal<McpServerDialog, void(void)> McpServerDialog::sig_done;

PEEL_CLASS_IMPL(McpServerDialog, "RookMcpServerDialog", Gtk::Window)

inline void McpServerDialog::Class::init()
{
    sig_done = Signal<McpServerDialog, void(void)>::create("done");
}

static void connectTransportToggles(
    Gtk::ToggleButton *a, Gtk::ToggleButton *b,
    Gtk::Stack *stack, std::string_view page_a, std::string_view page_b,
    std::function<void(bool is_page_a)> on_changed = {})
{
    a->connect_toggled([a, b, stack, page_a = std::string(page_a),
                        page_b = std::string(page_b),
                        on_changed](Gtk::ToggleButton *) {
        if (a->get_active()) {
            b->set_active(false);
            stack->set_visible_child_name(page_a.c_str());
            if (on_changed) on_changed(true);
        } else if (!b->get_active()) {
            a->set_active(true);
        }
    });

    b->connect_toggled([a, b, stack, page_a = std::string(page_a),
                        page_b = std::string(page_b),
                        on_changed](Gtk::ToggleButton *) {
        if (b->get_active()) {
            a->set_active(false);
            stack->set_visible_child_name(page_b.c_str());
            if (on_changed) on_changed(false);
        } else if (!a->get_active()) {
            b->set_active(true);
        }
    });
}

inline void McpServerDialog::init(Class *)
{
    set_title(_("Add MCP Server"));
    set_modal(true);
    set_default_size(480, 640);

    auto content = Gtk::Box::create(Gtk::Orientation::VERTICAL, 8);
    content->set_margin_start(16);
    content->set_margin_end(16);
    content->set_margin_top(16);
    content->set_margin_bottom(16);

    auto transport_label = Gtk::Label::create(_("Transport:"));
    transport_label->set_xalign(0.0f);
    content->append(std::move(transport_label));

    auto toggle_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
    toggle_box->add_css_class("linked");

    auto stdio_btn = Gtk::ToggleButton::create_with_label(_("Command"));
    stdio_btn->set_active(true);
    m_stdio_toggle = stdio_btn;
    toggle_box->append(std::move(stdio_btn));

    auto url_btn = Gtk::ToggleButton::create_with_label(_("URL"));
    m_url_toggle = url_btn;
    toggle_box->append(std::move(url_btn));

    content->append(std::move(toggle_box));

    auto name_label = Gtk::Label::create(_("Name:"));
    name_label->set_xalign(0.0f);
    content->append(std::move(name_label));

    auto id_entry = Gtk::Entry::create();
    id_entry->set_placeholder_text("my-server");
    m_id_entry = id_entry;
    content->append(std::move(id_entry));

    auto transport_stack = Gtk::Stack::create();

    auto stdio_page = Gtk::Box::create(Gtk::Orientation::VERTICAL, 6);

    auto cmd_label = Gtk::Label::create(_("Command:"));
    cmd_label->set_xalign(0.0f);
    stdio_page->append(std::move(cmd_label));

    auto cmd_entry = Gtk::Entry::create();
    cmd_entry->set_placeholder_text("npx");
    m_command_entry = cmd_entry;
    stdio_page->append(std::move(cmd_entry));

    auto args_label = Gtk::Label::create(_("Arguments:"));
    args_label->set_xalign(0.0f);
    stdio_page->append(std::move(args_label));

    auto args_list = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
    m_args_list = args_list;
    stdio_page->append(std::move(args_list));

    auto add_arg = Gtk::Button::create_with_label(_("+ Add Argument"));
    add_arg->connect_clicked([this](Gtk::Button *) { addArgEntry(); });
    stdio_page->append(std::move(add_arg));

    transport_stack->add_named(
        std::move(stdio_page).release_floating_ptr(), "stdio");

    auto url_page = Gtk::Box::create(Gtk::Orientation::VERTICAL, 6);

    auto url_label = Gtk::Label::create(_("URL:"));
    url_label->set_xalign(0.0f);
    url_page->append(std::move(url_label));

    auto url_entry = Gtk::Entry::create();
    url_entry->set_placeholder_text("https://localhost:3000/mcp");
    m_url_entry = url_entry;
    url_page->append(std::move(url_entry));

    auto hdrs_label = Gtk::Label::create(_("Headers:"));
    hdrs_label->set_xalign(0.0f);
    url_page->append(std::move(hdrs_label));

    auto headers_list = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
    m_headers_list = headers_list;
    url_page->append(std::move(headers_list));

    auto add_hdr = Gtk::Button::create_with_label(_("+ Add Header"));
    add_hdr->connect_clicked([this](Gtk::Button *) { addHeaderRow(); });
    url_page->append(std::move(add_hdr));

    transport_stack->add_named(
        std::move(url_page).release_floating_ptr(), "url");

    m_transport_stack = transport_stack;
    m_transport_stack->set_visible_child_name("stdio");
    content->append(std::move(transport_stack));

    connectTransportToggles(
        m_stdio_toggle, m_url_toggle,
        m_transport_stack, "stdio", "url",
        [this](bool is_stdio) {
            if (m_memory_row) m_memory_row->set_visible(is_stdio);
            if (m_cpu_row) m_cpu_row->set_visible(is_stdio);
        });

    auto cap_expander = Gtk::Expander::create(_("Capabilities"));
    cap_expander->set_expanded(false);

    auto cap_box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 6);
    cap_box->set_margin_start(24);
    cap_box->set_margin_top(8);

    auto read_label = Gtk::Label::create("");
    read_label->set_xalign(0.0f);
    read_label->set_use_markup(true);
    read_label->set_markup("<span weight=\"bold\">Read Paths</span>");
    cap_box->append(std::move(read_label));

    auto read_list = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
    m_read_paths_list = read_list;
    cap_box->append(std::move(read_list));

    auto add_read = Gtk::Button::create_with_label(_("+ Add Read Path"));
    add_read->connect_clicked([this](Gtk::Button *) { addReadPathEntry(); });
    cap_box->append(std::move(add_read));

    auto write_label = Gtk::Label::create("");
    write_label->set_xalign(0.0f);
    write_label->set_use_markup(true);
    write_label->set_markup("<span weight=\"bold\">Write Paths</span>");
    cap_box->append(std::move(write_label));

    auto write_list = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
    m_write_paths_list = write_list;
    cap_box->append(std::move(write_list));

    auto add_write = Gtk::Button::create_with_label(_("+ Add Write Path"));
    add_write->connect_clicked([this](Gtk::Button *) { addWritePathEntry(); });
    cap_box->append(std::move(add_write));

    auto net_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto net_label = Gtk::Label::create(_("Allow Network Access:"));
    net_label->set_xalign(0.0f);
    net_label->set_hexpand(true);
    auto net_switch = Gtk::Switch::create();
    m_network_switch = net_switch;
    net_switch->set_active(false);
    net_row->append(std::move(net_label));
    net_row->append(std::move(net_switch));
    cap_box->append(std::move(net_row));

    auto mem_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto mem_label = Gtk::Label::create(_("Memory Limit (MB):"));
    mem_label->set_xalign(0.0f);
    mem_label->set_hexpand(true);
    auto mem_spin = Gtk::SpinButton::create_with_range(0, 65536, 1);
    mem_spin->set_value(256);
    m_memory_spin = mem_spin;
    mem_row->append(std::move(mem_label));
    mem_row->append(std::move(mem_spin));
    m_memory_row = mem_row;
    cap_box->append(std::move(mem_row));

    auto cpu_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto cpu_label = Gtk::Label::create(_("CPU Time Limit (sec):"));
    cpu_label->set_xalign(0.0f);
    cpu_label->set_hexpand(true);
    auto cpu_spin = Gtk::SpinButton::create_with_range(0, 86400, 1);
    cpu_spin->set_value(60);
    m_cpu_spin = cpu_spin;
    cpu_row->append(std::move(cpu_label));
    cpu_row->append(std::move(cpu_spin));
    m_cpu_row = cpu_row;
    cap_box->append(std::move(cpu_row));

    cap_expander->set_child(std::move(cap_box).release_floating_ptr());
    m_cap_expander = cap_expander;
    content->append(std::move(cap_expander));

    auto enabled_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto enabled_label = Gtk::Label::create(_("Enabled:"));
    enabled_label->set_xalign(0.0f);
    enabled_label->set_hexpand(true);
    auto enabled_switch = Gtk::Switch::create();
    enabled_switch->set_active(true);
    m_enabled_switch = enabled_switch;
    enabled_row->append(std::move(enabled_label));
    enabled_row->append(std::move(enabled_switch));
    content->append(std::move(enabled_row));

    auto test_result = Gtk::Label::create("");
    test_result->set_xalign(0.0f);
    test_result->set_use_markup(true);
    m_test_result = test_result;
    content->append(std::move(test_result));

    auto button_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 8);
    button_bar->set_halign(Gtk::Align::END);
    button_bar->set_margin_top(8);

    auto test_btn = Gtk::Button::create_with_label(_("Test Connection"));
    m_test_button = test_btn;
    test_btn->connect_clicked([this](Gtk::Button *) { onTestConnection(); });
    button_bar->append(std::move(test_btn));

    auto cancel = Gtk::Button::create_with_label(_("Cancel"));
    cancel->connect_clicked([this](Gtk::Button *) { onCancel(); });
    button_bar->append(std::move(cancel));

    auto save = Gtk::Button::create_with_label(_("Save Server"));
    save->add_css_class("suggested-action");
    save->connect_clicked([this](Gtk::Button *) { onSave(); });
    button_bar->append(std::move(save));

    content->append(std::move(button_bar));

    auto sw = Gtk::ScrolledWindow::create();
    sw->set_child(std::move(content).release_floating_ptr());
    set_child(std::move(sw).release_floating_ptr());
}

FloatPtr<McpServerDialog> McpServerDialog::create(
    const rook::adapters::mcp::McpServerConfig *existing)
{
    auto dlg = Object::create<McpServerDialog>();

    if (existing) {
        dlg->m_edit_mode = true;
        dlg->m_edit_id = existing->id;
        dlg->set_title(_("Edit MCP Server"));

        dlg->m_id_entry->set_text(existing->id.c_str());
        dlg->m_id_entry->set_sensitive(false);
        dlg->m_enabled_switch->set_active(existing->enabled);

        if (existing->transport_type ==
            rook::adapters::mcp::McpTransportType::HttpSse)
        {
            dlg->m_url_toggle->set_active(true);
            dlg->m_stdio_toggle->set_active(false);
            dlg->m_transport_stack->set_visible_child_name("url");

            dlg->m_url_entry->set_text(existing->url.c_str());

            for (auto& [key, val] : existing->headers) {
                dlg->addHeaderRow(key, val);
            }

            if (dlg->m_memory_row) dlg->m_memory_row->set_visible(false);
            if (dlg->m_cpu_row) dlg->m_cpu_row->set_visible(false);
        } else {
            dlg->m_stdio_toggle->set_active(true);
            dlg->m_url_toggle->set_active(false);
            dlg->m_transport_stack->set_visible_child_name("stdio");

            dlg->m_command_entry->set_text(existing->command.c_str());

            for (auto& arg : existing->args) {
                dlg->addArgEntry(arg);
            }
        }
    }

    return dlg;
}

void McpServerDialog::onTransportChanged()
{
}

void McpServerDialog::onSave()
{
    if (!validateInput()) return;

    m_accepted = true;
    sig_done.emit(this);
    close();
}

bool McpServerDialog::validateInput()
{
    if (strlen(m_id_entry->get_text()) == 0) {
        setTestResult(false, "Name is required.");
        return false;
    }

    bool is_url = m_url_toggle->get_active();

    if (is_url) {
        if (strlen(m_url_entry->get_text()) == 0) {
            setTestResult(false, "URL is required.");
            return false;
        }
    } else {
        if (strlen(m_command_entry->get_text()) == 0) {
            setTestResult(false, "Command is required.");
            return false;
        }
    }

    return true;
}

void McpServerDialog::onCancel()
{
    m_accepted = false;
    close();
}

void McpServerDialog::onTestConnection()
{
    if (!validateInput()) return;

    auto cfg = getConfig();

    m_test_button->set_sensitive(false);
    m_test_button->set_label(_("Testing..."));
    setTestResult(false, "");

    (void)std::async(std::launch::async, [this, cfg = std::move(cfg)]() {
        try {
            std::unique_ptr<rook::adapters::mcp::McpTransport> transport;

            if (cfg.transport_type ==
                rook::adapters::mcp::McpTransportType::HttpSse)
            {
                transport = rook::adapters::mcp::makeHttpSseTransport(
                    cfg.url, cfg.headers);
            } else {
                transport = rook::adapters::mcp::makeStdioTransport(
                    cfg.command, cfg.args);
            }

            auto client =
                rook::adapters::mcp::makeMcpClient(std::move(transport));
            client->start();
            client->initialize("rook-test", "0.1.0");

            auto result = client->listTools();
            auto& tools_json = result["tools"];
            size_t count = tools_json.is_array() ? tools_json.size() : 0;

            std::string tool_names;
            if (tools_json.is_array()) {
                for (size_t i = 0; i < tools_json.size() && i < 5; ++i) {
                    if (i > 0) tool_names += ", ";
                    tool_names += tools_json[i]["name"].get<std::string>();
                }
                if (tools_json.size() > 5) tool_names += ", ...";
            }

            client->stop();

            GLib::idle_add_once([this, count, tool_names]() {
                setTestResult(true,
                    (std::to_string(count) + " tools found: " + tool_names).c_str());
                m_test_button->set_sensitive(true);
                m_test_button->set_label("Test Connection");
            });
        } catch (const std::exception& e) {
            GLib::idle_add_once([this, msg = std::string(e.what())]() {
                setTestResult(false, std::string("Failed: ") + msg);
                m_test_button->set_sensitive(true);
                m_test_button->set_label("Test Connection");
            });
        }
    });
}

void McpServerDialog::addArgEntry(std::string_view value)
{
    addArgEntryTo(m_args_list, m_arg_entries, value);
}

void McpServerDialog::addArgEntryTo(
    Gtk::Box *list,
    std::vector<Gtk::Entry*>& entries,
    std::string_view value)
{
    auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);

    auto entry = Gtk::Entry::create();
    entry->set_text(std::string(value).c_str());
    entry->set_hexpand(true);
    Gtk::Entry *eptr = entry;
    entries.push_back(eptr);
    row->append(std::move(entry));

    auto remove = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
    Gtk::Box *raw_row = row;
    remove->connect_clicked([this, eptr, raw_row, list, &entries](Gtk::Button *) {
        list->remove(raw_row);
        auto it = std::find(entries.begin(), entries.end(), eptr);
        if (it != entries.end()) entries.erase(it);
    });
    row->append(std::move(remove));

    list->append(std::move(row));
}

void McpServerDialog::addHeaderRow(std::string_view key, std::string_view value)
{
    auto key_entry = Gtk::Entry::create();
    key_entry->set_placeholder_text(_("Key"));
    key_entry->set_text(std::string(key).c_str());
    key_entry->set_hexpand(true);

    auto value_entry = Gtk::Entry::create();
    value_entry->set_placeholder_text(_("Value"));
    value_entry->set_text(std::string(value).c_str());
    value_entry->set_hexpand(true);

    HeaderRow hr;
    hr.key_entry = key_entry;
    hr.value_entry = value_entry;

    auto header_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
    hr.row = header_row;

    header_row->append(std::move(key_entry));
    header_row->append(std::move(value_entry));

    auto remove = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
    auto* hr_ptr = &m_header_rows.emplace_back(std::move(hr));
    remove->connect_clicked([this, hr_ptr](Gtk::Button *) {
        m_headers_list->remove(hr_ptr->row);
        auto it = std::find_if(m_header_rows.begin(), m_header_rows.end(),
            [hr_ptr](auto& h) { return &h == hr_ptr; });
        if (it != m_header_rows.end()) m_header_rows.erase(it);
    });
    header_row->append(std::move(remove));

    m_headers_list->append(std::move(header_row));
}

void McpServerDialog::addReadPathEntry(std::string_view value)
{
    addPathEntry(m_read_paths_list, m_read_path_entries, value);
}

void McpServerDialog::addWritePathEntry(std::string_view value)
{
    addPathEntry(m_write_paths_list, m_write_path_entries, value);
}

void McpServerDialog::addPathEntry(
    Gtk::Box *list,
    std::vector<Gtk::Entry*>& entries,
    std::string_view value)
{
    auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);

    auto entry = Gtk::Entry::create();
    entry->set_text(std::string(value).c_str());
    entry->set_hexpand(true);
    Gtk::Entry *eptr = entry;
    entries.push_back(eptr);
    row->append(std::move(entry));

    auto remove = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
    std::vector<Gtk::Entry*> *entries_ptr = &entries;
    Gtk::Box *raw_row = row;
    remove->connect_clicked([this, eptr, raw_row, list, entries_ptr](Gtk::Button *) {
        list->remove(raw_row);
        auto it = std::find(entries_ptr->begin(), entries_ptr->end(), eptr);
        if (it != entries_ptr->end()) entries_ptr->erase(it);
    });
    row->append(std::move(remove));

    list->append(std::move(row));
}

std::vector<std::string> McpServerDialog::collectReadPaths() const
{
    std::vector<std::string> result;
    for (auto* entry : m_read_path_entries) {
        auto text = std::string(entry->get_text());
        if (!text.empty()) result.push_back(std::move(text));
    }
    return result;
}

std::vector<std::string> McpServerDialog::collectArgs() const
{
    std::vector<std::string> result;
    for (auto* entry : m_arg_entries) {
        auto text = std::string(entry->get_text());
        if (!text.empty()) result.push_back(std::move(text));
    }
    return result;
}

std::vector<std::string> McpServerDialog::collectWritePaths() const
{
    std::vector<std::string> result;
    for (auto* entry : m_write_path_entries) {
        auto text = std::string(entry->get_text());
        if (!text.empty()) result.push_back(std::move(text));
    }
    return result;
}

void McpServerDialog::setTestResult(bool ok, std::string_view message)
{
    if (ok) {
        m_test_result->set_markup(
            (std::string("<span foreground=\"#33d17a\">✓</span> ") +
             std::string(message)).c_str());
    } else if (!message.empty()) {
        m_test_result->set_markup(
            (std::string("<span foreground=\"#e01b24\">✕</span> ") +
             std::string(message)).c_str());
    } else {
        m_test_result->set_text("");
    }
}

rook::adapters::mcp::McpServerConfig McpServerDialog::getConfig() const
{
    rook::adapters::mcp::McpServerConfig cfg;

    cfg.id = std::string(m_id_entry->get_text());
    cfg.enabled = m_enabled_switch->get_active();

    bool is_url = m_url_toggle->get_active();

    if (is_url) {
        cfg.transport_type = rook::adapters::mcp::McpTransportType::HttpSse;
        cfg.url = std::string(m_url_entry->get_text());

        for (auto& hr : m_header_rows) {
            auto key = std::string(hr.key_entry->get_text());
            auto val = std::string(hr.value_entry->get_text());
            if (!key.empty()) {
                cfg.headers.emplace_back(std::move(key), std::move(val));
            }
        }
    } else {
        cfg.transport_type = rook::adapters::mcp::McpTransportType::Stdio;
        cfg.command = std::string(m_command_entry->get_text());
        cfg.args = collectArgs();
    }

    return cfg;
}

rook::adapters::security::Capability McpServerDialog::getCapability() const
{
    auto builder = rook::adapters::security::Capability::grant();

    for (auto* entry : m_read_path_entries) {
        auto text = std::string(entry->get_text());
        if (!text.empty()) builder.read(std::move(text));
    }
    for (auto* entry : m_write_path_entries) {
        auto text = std::string(entry->get_text());
        if (!text.empty()) builder.write(std::move(text));
    }

    if (m_network_switch->get_active()) {
        builder.allowNetwork();
    } else {
        builder.noNetwork();
    }

    builder.maxMemoryMb(static_cast<int64_t>(m_memory_spin->get_value()));
    builder.maxCpuTime(std::chrono::seconds(
        static_cast<int64_t>(m_cpu_spin->get_value())));

    return builder.build();
}

void McpServerDialog::addReadPath(std::string_view value)
{
    addPathEntry(m_read_paths_list, m_read_path_entries, value);
}

void McpServerDialog::addWritePath(std::string_view value)
{
    addPathEntry(m_write_paths_list, m_write_path_entries, value);
}

void McpServerDialog::setNetworkAllowed(bool allowed)
{
    m_network_switch->set_active(allowed);
}

void McpServerDialog::setMemoryMb(int64_t mb)
{
    m_memory_spin->set_value(static_cast<double>(mb));
}

void McpServerDialog::setCpuSecs(int64_t secs)
{
    m_cpu_spin->set_value(static_cast<double>(secs));
}

void McpServerDialog::setEnabled(bool e)
{
    m_enabled_switch->set_active(e);
}

void McpServerDialog::setPresetIndex(guint)
{
}

} // namespace rook::gui
