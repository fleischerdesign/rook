#include <glib/gi18n.h>
#include "extension_settings_page.hpp"
#include "extension_install_dialog.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>
#include <spdlog/spdlog.h>

using namespace peel;

namespace rook::gui {

Signal<ExtensionSettingsPage, void(void)> ExtensionSettingsPage::sig_changed;

PEEL_CLASS_IMPL(ExtensionSettingsPage, "RookExtensionSettingsPage", Gtk::Box)

inline void ExtensionSettingsPage::Class::init()
{
    sig_changed = Signal<ExtensionSettingsPage, void(void)>::create("changed");
}

inline void ExtensionSettingsPage::init(Class *)
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

FloatPtr<ExtensionSettingsPage> ExtensionSettingsPage::create(
    rook::ports::ExtensionPort *extensions,
    rook::adapters::mcp::McpServerManager *mcp)
{
    auto page = Object::create<ExtensionSettingsPage>();
    page->m_extensions = extensions;
    page->m_mcp = mcp;

    auto heading = Gtk::Label::create(_("Extensions"));
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    page->append(std::move(heading));

    auto desc = Gtk::Label::create(
        "Install extensions from GitHub to add MCP servers, skills, and commands.");
    desc->set_xalign(0.0f);
    desc->add_css_class("dim-label");
    desc->set_wrap(true);
    desc->set_margin_bottom(4);
    page->append(std::move(desc));

    auto stack = Gtk::Stack::create();
    stack->set_vexpand(true);

    auto empty = Adw::StatusPage::create();
    empty->set_title(_("No Extensions Installed"));
    empty->set_description(
        "Install extensions from GitHub to give the assistant new capabilities.");
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

    ExtensionSettingsPage *raw_page = page;

    auto install_btn = Gtk::Button::create_with_label(_("Install from GitHub"));
    install_btn->add_css_class("suggested-action");
    install_btn->connect_clicked([raw_page](Gtk::Button *) {
        raw_page->onInstallFromUrl();
    });
    button_bar->append(std::move(install_btn));

    page->append(std::move(button_bar));

    page->refreshList();
    return page;
}

void ExtensionSettingsPage::refreshList()
{
    while (auto *row = m_list->get_row_at_index(0)) {
        m_list->remove(row);
    }

    auto installed = m_extensions->listInstalled();

    if (installed.empty()) {
        m_stack->set_visible_child_name("empty");
        return;
    }

    m_stack->set_visible_child_name("list");

    for (auto& ext : installed) {
        auto row = Adw::ExpanderRow::create();

        auto title = ext.display_name;
        if (!ext.version.empty()) title += " v" + ext.version;
        row->set_title(title.c_str());

        std::string subtitle = ext.description;
        if (!ext.author.empty()) subtitle += " — by " + ext.author;
        row->set_subtitle(subtitle.c_str());

        if (!ext.mcp_servers.empty()) {
            auto mcp_label = Adw::ActionRow::create();
            mcp_label->set_title(_("MCP Servers"));
            mcp_label->set_subtitle(
                (std::to_string(ext.mcp_servers.size()) + " server"
                 + (ext.mcp_servers.size() > 1 ? "s" : "") + " provided").c_str());
            row->add_row(std::move(mcp_label).release_floating_ptr());

            for (auto& srv : ext.mcp_servers) {
                auto srv_row = Adw::ActionRow::create();
                auto srv_title = srv.id;
                srv_row->set_title(srv_title.c_str());

                std::string srv_subtitle;
                if (!srv.url.empty()) {
                    srv_subtitle = "http · " + srv.url;
                } else if (!srv.command.empty()) {
                    srv_subtitle = "stdio · " + srv.command;
                    for (auto& a : srv.args) srv_subtitle += " " + a;
                }
                srv_row->set_subtitle(srv_subtitle.c_str());
                srv_row->set_use_markup(false);

                auto state_lbl = Gtk::Label::create(
                    srv.enabled ? "enabled" : "disabled");
                state_lbl->add_css_class(
                    srv.enabled ? "success" : "dim-label");
                srv_row->add_suffix(std::move(state_lbl).release_floating_ptr());

                row->add_row(std::move(srv_row).release_floating_ptr());
            }
        }

        if (!ext.skills.empty()) {
            auto skill_label = Adw::ActionRow::create();
            skill_label->set_title("Skills");
            skill_label->set_subtitle(
                (std::to_string(ext.skills.size()) + " skill"
                 + (ext.skills.size() > 1 ? "s" : "") + " provided").c_str());
            row->add_row(std::move(skill_label).release_floating_ptr());
        }

        if (!ext.commands.empty()) {
            auto cmd_label = Adw::ActionRow::create();
            cmd_label->set_title(_("Commands"));
            cmd_label->set_subtitle(
                (std::to_string(ext.commands.size()) + " command"
                 + (ext.commands.size() > 1 ? "s" : "")).c_str());
            row->add_row(std::move(cmd_label).release_floating_ptr());
        }

        if (!ext.context_files.empty()) {
            auto cf_label = Adw::ActionRow::create();
            cf_label->set_title(_("Context Files"));
            std::string names;
            for (auto& cf : ext.context_files) {
                if (!names.empty()) names += ", ";
                names += cf.path;
            }
            cf_label->set_subtitle(names.c_str());
            row->add_row(std::move(cf_label).release_floating_ptr());
        }

        std::string name = ext.name;
        std::string url = ext.url;

        auto uninstall_btn = Gtk::Button::create_with_label(_("Uninstall"));
        uninstall_btn->add_css_class("destructive-action");
        uninstall_btn->connect_clicked([this, name](Gtk::Button *) {
            if (m_extensions->uninstall(name)) {
                m_mcp->stopAll();
                m_mcp->startAll();
                refreshList();
                sig_changed.emit(this);
            }
        });
        row->add_suffix(std::move(uninstall_btn).release_floating_ptr());

        auto update_btn = Gtk::Button::create_with_label(_("Update"));
        update_btn->connect_clicked([this, name](Gtk::Button *) {
            if (m_extensions->update(name)) {
                m_mcp->stopAll();
                m_mcp->startAll();
                refreshList();
                sig_changed.emit(this);
            }
        });
        row->add_suffix(std::move(update_btn).release_floating_ptr());

        m_list->append(std::move(row).release_floating_ptr());
    }
}

void ExtensionSettingsPage::onInstallFromUrl()
{
    auto dialog = ExtensionInstallDialog::create();
    ExtensionInstallDialog *raw_dlg = dialog;

    raw_dlg->connect_done([raw_dlg, this](ExtensionInstallDialog *) {
        if (!raw_dlg->wasAccepted()) return;

        auto url = raw_dlg->url();

        if (m_extensions->install(url)) {
            if (m_mcp) {
                m_mcp->stopAll();
                m_mcp->startAll();
            }
            refreshList();
            sig_changed.emit(this);
        } else {
            spdlog::warn("ExtensionSettingsPage: install failed for {}", url);
        }
        raw_dlg->close();
    });
    raw_dlg->present();
}

} // namespace rook::gui
