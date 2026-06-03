#include "mcp_settings_page.hpp"
#include "mcp_server_dialog.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>
#include <spdlog/spdlog.h>

using namespace peel;

namespace rook::gui {

Signal<McpSettingsPage, void(void)> McpSettingsPage::sig_changed;

PEEL_CLASS_IMPL(McpSettingsPage, "RookMcpSettingsPage", Gtk::Box)

inline void McpSettingsPage::Class::init()
{
    sig_changed = Signal<McpSettingsPage, void(void)>::create("changed");
}

inline void McpSettingsPage::init(Class *)
{
    gtk_orientable_set_orientation(
        GTK_ORIENTABLE(reinterpret_cast<::GtkBox*>(this)),
        GTK_ORIENTATION_VERTICAL);
    set_spacing(12);
    set_margin_start(12);
    set_margin_end(12);
    set_margin_top(12);
    set_margin_bottom(12);
}

FloatPtr<McpSettingsPage> McpSettingsPage::create(
    rook::adapters::mcp::McpServerManager *mcp,
    rook::adapters::security::SecurityManager *security)
{
    auto page = Object::create<McpSettingsPage>();
    page->m_mcp = mcp;
    page->m_security = security;

    auto heading = Gtk::Label::create("Tools");
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    page->append(std::move(heading));

    auto desc = Gtk::Label::create(
        "Tools the AI assistant can use to read files, search the web, and more.");
    desc->set_xalign(0.0f);
    desc->add_css_class("dim-label");
    desc->set_wrap(true);
    desc->set_margin_bottom(4);
    page->append(std::move(desc));

    auto builtin_expander = Gtk::Expander::create("Built-in");
    builtin_expander->set_expanded(false);

    auto builtin_box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 4);
    builtin_box->set_margin_top(4);
    builtin_box->set_margin_start(12);

    auto builtin_hint = Gtk::Label::create(
        "Always available, run locally on your computer.");
    builtin_hint->set_xalign(0.0f);
    builtin_hint->add_css_class("dim-label");
    builtin_hint->set_margin_bottom(6);
    builtin_box->append(std::move(builtin_hint));

    auto builtin_list = Gtk::ListBox::create();
    builtin_list->add_css_class("boxed-list");
    builtin_list->set_selection_mode(Gtk::SelectionMode::NONE);

    struct { const char* name; const char* desc; } builtins[] = {
        {"read_file", "Read the contents of a file at the given path"},
        {"write_file", "Create or overwrite a file at the given path"},
        {"list_directory", "List files and directories at the given path"},
    };
    for (auto& bt : builtins) {
        auto row = Adw::ActionRow::create();
        row->set_title(bt.name);
        row->set_subtitle(bt.desc);
        builtin_list->append(std::move(row).release_floating_ptr());
    }
    builtin_box->append(std::move(builtin_list));
    builtin_expander->set_child(std::move(builtin_box).release_floating_ptr());
    page->append(std::move(builtin_expander));

    auto mcp_header = Gtk::Label::create("");
    mcp_header->set_xalign(0.0f);
    mcp_header->set_use_markup(true);
    mcp_header->set_markup("<span weight=\"bold\" size=\"small\">MCP Servers</span>");
    mcp_header->set_margin_top(16);
    page->m_mcp_header = mcp_header;
    page->append(std::move(mcp_header));

    auto mcp_hint = Gtk::Label::create(
        "External tools from MCP servers. Each server can provide multiple tools.");
    mcp_hint->set_xalign(0.0f);
    mcp_hint->add_css_class("dim-label");
    mcp_hint->set_margin_bottom(4);
    page->append(std::move(mcp_hint));

    auto stack = Gtk::Stack::create();
    stack->set_vexpand(true);

    auto empty = Adw::StatusPage::create();
    empty->set_title("No MCP Servers");
    empty->set_description("Add servers to give the assistant access to external tools.");
    empty->add_css_class("compact");
    stack->add_named(std::move(empty).release_floating_ptr(), "empty");

    auto list = Gtk::ListBox::create();
    list->add_css_class("boxed-list");
    list->set_selection_mode(Gtk::SelectionMode::NONE);
    Gtk::ListBox *lptr = list;
    page->m_list = lptr;
    stack->add_named(std::move(list).release_floating_ptr(), "list");

    Gtk::Stack *sptr = stack;
    page->m_stack = sptr;
    page->append(std::move(stack));

    auto button_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 8);
    button_bar->set_halign(Gtk::Align::START);
    button_bar->set_margin_top(4);

    McpSettingsPage *raw_page = page;

    auto add = Gtk::Button::create_with_label("Add Server");
    add->add_css_class("suggested-action");
    add->connect_clicked([raw_page](Gtk::Button *) {
        raw_page->onAddServer();
    });
    button_bar->append(std::move(add));

    page->append(std::move(button_bar));

    page->refreshList();
    return page;
}

bool McpSettingsPage::isExtensionServer(std::string_view id) const
{
    auto servers = m_mcp->listServers();
    for (auto& s : servers) {
        if (s.id == id) return !s.source.empty() && s.source.starts_with("extension:");
    }
    return false;
}

void McpSettingsPage::refreshList()
{
    while (auto *row = m_list->get_row_at_index(0)) {
        m_list->remove(row);
    }

    auto servers = m_mcp->listServers();

    if (servers.empty()) {
        m_stack->set_visible_child_name("empty");
        if (m_mcp_header)
            m_mcp_header->set_markup(
                "<span weight=\"bold\" size=\"small\">MCP Servers</span>");
        return;
    }

    m_stack->set_visible_child_name("list");

    size_t total_tools = 0;
    for (auto& srv : servers) {
        total_tools += m_mcp->toolCountForServer(srv.id);
    }

    if (m_mcp_header) {
        auto header_text = std::string("<span weight=\"bold\" size=\"small\">MCP Servers</span>"
            " <span alpha=\"55%\" size=\"small\">· ") +
            std::to_string(servers.size()) + " server" +
            (servers.size() > 1 ? "s" : "") +
            ", " + std::to_string(total_tools) + " tool" +
            (total_tools > 1 ? "s" : "") + "</span>";
        m_mcp_header->set_markup(header_text.c_str());
    }

    for (auto& srv : servers) {
        auto tool_count = m_mcp->toolCountForServer(srv.id);
        auto all_tools = m_mcp->listAllTools();

        auto expander = Adw::ExpanderRow::create();

        auto title = srv.id;
        if (tool_count > 0) {
            title += " · " + std::to_string(tool_count) + " tool"
                   + (tool_count > 1 ? "s" : "");
        }
        expander->set_title(title.c_str());

        std::string subtitle;
        if (srv.transport_type == rook::adapters::mcp::McpTransportType::HttpSse) {
            subtitle = "http · " + srv.url;
        } else {
            subtitle = "stdio · " + srv.command;
            for (auto& a : srv.args) subtitle += " " + a;
        }

        if (!srv.source.empty() && srv.source.starts_with("extension:")) {
            auto ext_name = srv.source.substr(10);
            subtitle += " · via " + ext_name;
        }

        expander->set_subtitle(subtitle.c_str());

        for (auto& tool : all_tools) {
            auto expected_source = std::string("mcp:") + srv.id;
            if (tool.source != expected_source) continue;

            auto tool_row = Adw::ActionRow::create();
            tool_row->set_title(tool.name.c_str());
            if (!tool.description.empty()) {
                tool_row->set_subtitle(tool.description.c_str());
            }
            expander->add_row(std::move(tool_row).release_floating_ptr());
        }

        auto enabled_switch = Gtk::Switch::create();
        enabled_switch->set_active(srv.enabled);
        std::string sid_enable = srv.id;
        McpSettingsPage *raw_page_enable = this;
        enabled_switch->connect_state_set([raw_page_enable, sid_enable](Gtk::Switch *, bool state) {
            raw_page_enable->m_mcp->setEnabled(sid_enable, state);
            raw_page_enable->sig_changed.emit(raw_page_enable);
            return false;
        });
        expander->add_suffix(std::move(enabled_switch).release_floating_ptr());

        bool is_ext = !srv.source.empty() && srv.source.starts_with("extension:");

        if (is_ext) {
            std::string sid_ovr = srv.id;
            McpSettingsPage *raw_page_ovr = this;

            auto caps_btn = Gtk::Button::create_with_label("Capabilities");
            caps_btn->connect_clicked([raw_page_ovr, sid_ovr](Gtk::Button *) {
                raw_page_ovr->onOverrideCapabilities(sid_ovr);
            });
            expander->add_suffix(std::move(caps_btn).release_floating_ptr());

            auto managed_label = Gtk::Label::create("via extension");
            managed_label->add_css_class("dim-label");
            managed_label->set_margin_end(8);
            expander->add_suffix(std::move(managed_label).release_floating_ptr());
        } else {
            std::string sid = srv.id;
            McpSettingsPage *raw_page = this;

            auto edit_btn = Gtk::Button::create_with_label("Edit");
            edit_btn->connect_clicked([raw_page, sid](Gtk::Button *) {
                raw_page->onEditServer(sid);
            });
            expander->add_suffix(std::move(edit_btn).release_floating_ptr());

            auto del_btn = Gtk::Button::create_with_label("Delete");
            del_btn->add_css_class("destructive-action");
            del_btn->connect_clicked([raw_page, sid](Gtk::Button *) {
                raw_page->onDeleteServer(sid);
            });
            expander->add_suffix(std::move(del_btn).release_floating_ptr());
        }

        m_list->append(std::move(expander).release_floating_ptr());
    }
}

void McpSettingsPage::onAddServer()
{
    auto dialog = McpServerDialog::create();
    McpServerDialog *raw_dlg = dialog;
    McpSettingsPage *raw_page = this;

    raw_dlg->connect_done([raw_dlg, raw_page](McpServerDialog *) {
        if (!raw_dlg->wasAccepted()) return;
        auto cfg = raw_dlg->getConfig();
        cfg.source = "manual";
        auto cap = raw_dlg->getCapability();
        auto server_id = cfg.id;
        bool has_caps = !cap.readPaths().empty()
                     || !cap.writePaths().empty()
                     || cap.allowsNetwork()
                     || cap.maxMemoryMb() != 256
                     || cap.maxCpuTimeSecs() != 60;
        raw_page->m_mcp->addServer(std::move(cfg));
        raw_page->m_mcp->stopAll();
        raw_page->m_mcp->startAll();
        if (has_caps)
            raw_page->m_security->setCapability(server_id, std::move(cap));
        raw_page->refreshList();
        raw_page->sig_changed.emit(raw_page);
        raw_dlg->close();
    });
    raw_dlg->present();
}

void McpSettingsPage::onEditServer(std::string_view id)
{
    if (isExtensionServer(id)) return;

    auto servers = m_mcp->listServers();
    rook::adapters::mcp::McpServerConfig existing;
    bool found = false;
    for (auto& s : servers) {
        if (s.id == id) { existing = s; found = true; break; }
    }
    if (!found) return;

    auto dialog = McpServerDialog::create(&existing);
    McpServerDialog *raw_dlg = dialog;
    McpSettingsPage *raw_page = this;

    if (auto* cap = m_security->findCapability(id)) {
        for (auto& p : cap->readPaths()) raw_dlg->addReadPath(p);
        for (auto& p : cap->writePaths()) raw_dlg->addWritePath(p);
        raw_dlg->setNetworkAllowed(cap->allowsNetwork());
        raw_dlg->setMemoryMb(cap->maxMemoryMb());
        raw_dlg->setCpuSecs(cap->maxCpuTimeSecs());
    }

    raw_dlg->connect_done([raw_dlg, raw_page, existing_source = existing.source](McpServerDialog *) {
        if (!raw_dlg->wasAccepted()) return;
        auto cfg = raw_dlg->getConfig();
        cfg.source = existing_source.empty() ? "manual" : existing_source;
        auto cap = raw_dlg->getCapability();
        auto server_id = cfg.id;
        bool has_caps = !cap.readPaths().empty()
                     || !cap.writePaths().empty()
                     || cap.allowsNetwork()
                     || cap.maxMemoryMb() != 256
                     || cap.maxCpuTimeSecs() != 60;
        raw_page->m_mcp->removeServer(server_id);
        raw_page->m_mcp->addServer(std::move(cfg));
        raw_page->m_mcp->stopAll();
        raw_page->m_mcp->startAll();
        if (has_caps)
            raw_page->m_security->setCapability(server_id, std::move(cap));
        raw_page->refreshList();
        raw_page->sig_changed.emit(raw_page);
        raw_dlg->close();
    });
    raw_dlg->present();
}

void McpSettingsPage::onDeleteServer(std::string_view id)
{
    if (isExtensionServer(id)) return;

    auto msg = std::string("Delete '") + std::string(id) + "'?\n"
             + "This server and all its tools will be removed. This cannot be undone.";

    auto dlg = Adw::AlertDialog::create("Delete MCP Server", msg.c_str());
    dlg->add_response("cancel", "Cancel");
    dlg->add_response("delete", "Delete");
    dlg->set_response_appearance("delete", Adw::ResponseAppearance::DESTRUCTIVE);
    dlg->set_default_response("cancel");
    dlg->set_close_response("cancel");

    McpSettingsPage *raw_page = this;
    std::string sid {id};
    dlg->connect_response([raw_page, sid](Adw::AlertDialog *, const char *response) {
        if (!g_strcmp0(response, "delete")) {
            raw_page->m_mcp->removeServer(sid);
            raw_page->refreshList();
            raw_page->sig_changed.emit(raw_page);
        }
    });

    dlg->present(this);
}

void McpSettingsPage::onOverrideCapabilities(std::string_view id)
{
    auto dlg = Gtk::Window::create();
    dlg->set_title("Capability Override");
    dlg->set_modal(true);
    dlg->set_default_size(400, 420);

    auto content = Gtk::Box::create(Gtk::Orientation::VERTICAL, 8);
    content->set_margin_start(16);
    content->set_margin_end(16);
    content->set_margin_top(16);
    content->set_margin_bottom(16);

    auto heading = Gtk::Label::create("");
    heading->set_xalign(0.0f);
    heading->set_use_markup(true);
    heading->set_markup(("<span weight=\"bold\">" + std::string(id) + "</span>").c_str());
    content->append(std::move(heading));

    auto hint = Gtk::Label::create("Override the security bounds for this server. "
        "Empty paths and disabled network mean unrestricted access.");
    hint->set_xalign(0.0f);
    hint->add_css_class("dim-label");
    hint->set_wrap(true);
    content->append(std::move(hint));

    struct PathEntry {
        Gtk::Entry* entry = nullptr;
        Gtk::Box* row = nullptr;
    };
    std::vector<PathEntry> read_paths;
    std::vector<PathEntry> write_paths;

    auto read_label = Gtk::Label::create("");
    read_label->set_xalign(0.0f);
    read_label->set_use_markup(true);
    read_label->set_markup("<span weight=\"bold\">Read Paths</span>");
    content->append(std::move(read_label));

    auto read_list = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
    Gtk::Box* rl = read_list;
    content->append(std::move(read_list));

    auto add_read = Gtk::Button::create_with_label("+ Add Read Path");
    add_read->connect_clicked([rl, &read_paths](Gtk::Button *) {
        auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
        auto entry = Gtk::Entry::create();
        entry->set_hexpand(true);
        Gtk::Box* rr = row;
        Gtk::Entry* ee = entry;
        read_paths.push_back({ee, rr});
        row->append(std::move(entry));
        auto del = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
        auto* pe = &read_paths.back();
        del->connect_clicked([rl, pe, &read_paths](Gtk::Button *) {
            rl->remove(pe->row);
            auto it = std::find_if(read_paths.begin(), read_paths.end(),
                [pe](auto& p) { return &p == pe; });
            if (it != read_paths.end()) read_paths.erase(it);
        });
        row->append(std::move(del));
        rl->append(std::move(row));
    });
    content->append(std::move(add_read));

    auto write_label = Gtk::Label::create("");
    write_label->set_xalign(0.0f);
    write_label->set_use_markup(true);
    write_label->set_markup("<span weight=\"bold\">Write Paths</span>");
    content->append(std::move(write_label));

    auto write_list = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
    Gtk::Box* wl = write_list;
    content->append(std::move(write_list));

    auto add_write = Gtk::Button::create_with_label("+ Add Write Path");
    add_write->connect_clicked([wl, &write_paths](Gtk::Button *) {
        auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
        auto entry = Gtk::Entry::create();
        entry->set_hexpand(true);
        Gtk::Box* rr = row;
        Gtk::Entry* ee = entry;
        write_paths.push_back({ee, rr});
        row->append(std::move(entry));
        auto del = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
        auto* pe = &write_paths.back();
        del->connect_clicked([wl, pe, &write_paths](Gtk::Button *) {
            wl->remove(pe->row);
            auto it = std::find_if(write_paths.begin(), write_paths.end(),
                [pe](auto& p) { return &p == pe; });
            if (it != write_paths.end()) write_paths.erase(it);
        });
        row->append(std::move(del));
        wl->append(std::move(row));
    });
    content->append(std::move(add_write));

    auto net_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto net_label = Gtk::Label::create("Allow Network Access:");
    net_label->set_xalign(0.0f);
    net_label->set_hexpand(true);
    auto net_switch = Gtk::Switch::create();
    Gtk::Switch* ns_ptr = net_switch;
    net_row->append(std::move(net_label));
    net_row->append(std::move(net_switch));
    content->append(std::move(net_row));

    auto mem_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto mem_label = Gtk::Label::create("Memory Limit (MB):");
    mem_label->set_xalign(0.0f);
    mem_label->set_hexpand(true);
    auto mem_spin = Gtk::SpinButton::create_with_range(0, 65536, 1);
    mem_spin->set_value(256);
    Gtk::SpinButton* ms_ptr = mem_spin;
    mem_row->append(std::move(mem_label));
    mem_row->append(std::move(mem_spin));
    content->append(std::move(mem_row));

    auto cpu_row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 12);
    auto cpu_label = Gtk::Label::create("CPU Time Limit (sec):");
    cpu_label->set_xalign(0.0f);
    cpu_label->set_hexpand(true);
    auto cpu_spin = Gtk::SpinButton::create_with_range(0, 86400, 1);
    cpu_spin->set_value(60);
    Gtk::SpinButton* cs_ptr = cpu_spin;
    cpu_row->append(std::move(cpu_label));
    cpu_row->append(std::move(cpu_spin));
    content->append(std::move(cpu_row));

    auto button_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 8);
    button_bar->set_halign(Gtk::Align::END);
    button_bar->set_margin_top(8);

    auto cancel_btn = Gtk::Button::create_with_label("Cancel");
    Gtk::Window* raw_dlg = dlg;
    cancel_btn->connect_clicked([raw_dlg](Gtk::Button *) { raw_dlg->close(); });
    button_bar->append(std::move(cancel_btn));

    auto save_btn = Gtk::Button::create_with_label("Save");
    save_btn->add_css_class("suggested-action");

    McpSettingsPage* raw_page = this;
    std::string sid {id};
    save_btn->connect_clicked([raw_page, sid, raw_dlg,
        &read_paths, &write_paths, ns_ptr, ms_ptr, cs_ptr](Gtk::Button *) {
        auto builder = rook::adapters::security::Capability::grant();
        for (auto& p : read_paths) {
            auto t = std::string(p.entry->get_text());
            if (!t.empty()) builder.read(std::move(t));
        }
        for (auto& p : write_paths) {
            auto t = std::string(p.entry->get_text());
            if (!t.empty()) builder.write(std::move(t));
        }
        if (ns_ptr->get_active()) builder.allowNetwork();
        else builder.noNetwork();
        builder.maxMemoryMb(static_cast<int64_t>(ms_ptr->get_value()));
        builder.maxCpuTime(std::chrono::seconds(
            static_cast<int64_t>(cs_ptr->get_value())));

        raw_page->m_security->setCapability(sid, builder.build());
        raw_page->refreshList();
        raw_page->sig_changed.emit(raw_page);
        raw_dlg->close();
    });
    button_bar->append(std::move(save_btn));

    content->append(std::move(button_bar));

    if (auto* existing_cap = m_security->findCapability(id)) {
        for (auto& p : existing_cap->readPaths()) {
            auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
            auto entry = Gtk::Entry::create();
            entry->set_text(p.c_str());
            entry->set_hexpand(true);
            Gtk::Box* rr = row;
            Gtk::Entry* ee = entry;
            read_paths.push_back({ee, rr});
            row->append(std::move(entry));
            auto del = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
            auto* pe = &read_paths.back();
            del->connect_clicked([rl, pe, &read_paths](Gtk::Button *) {
                rl->remove(pe->row);
                auto it = std::find_if(read_paths.begin(), read_paths.end(),
                    [pe](auto& x) { return &x == pe; });
                if (it != read_paths.end()) read_paths.erase(it);
            });
            row->append(std::move(del));
            rl->append(std::move(row));
        }
        for (auto& p : existing_cap->writePaths()) {
            auto row = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 4);
            auto entry = Gtk::Entry::create();
            entry->set_text(p.c_str());
            entry->set_hexpand(true);
            Gtk::Box* rr = row;
            Gtk::Entry* ee = entry;
            write_paths.push_back({ee, rr});
            row->append(std::move(entry));
            auto del = Gtk::Button::create_from_icon_name("edit-delete-symbolic");
            auto* pe = &write_paths.back();
            del->connect_clicked([wl, pe, &write_paths](Gtk::Button *) {
                wl->remove(pe->row);
                auto it = std::find_if(write_paths.begin(), write_paths.end(),
                    [pe](auto& x) { return &x == pe; });
                if (it != write_paths.end()) write_paths.erase(it);
            });
            row->append(std::move(del));
            wl->append(std::move(row));
        }
        ns_ptr->set_active(existing_cap->allowsNetwork());
        ms_ptr->set_value(static_cast<double>(existing_cap->maxMemoryMb()));
        cs_ptr->set_value(static_cast<double>(existing_cap->maxCpuTimeSecs()));
    }

    auto sw = Gtk::ScrolledWindow::create();
    sw->set_child(std::move(content).release_floating_ptr());
    dlg->set_child(std::move(sw).release_floating_ptr());

    dlg->present();
}

} // namespace rook::gui
