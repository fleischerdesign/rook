#include <glib/gi18n.h>
#include "extension_settings_page.hpp"
#include "extension_install_dialog.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include <peel/Adw/Adw.h>
#include <gtk/gtk.h>
#include <spdlog/spdlog.h>

using namespace peel;

namespace rook::gui {

std::unique_ptr<ExtensionSettingsPage> ExtensionSettingsPage::create(
    rook::ports::ExtensionPort *extensions,
    rook::adapters::mcp::McpServerManager *mcp,
    ChangeFn on_changed)
{
    auto page = std::unique_ptr<ExtensionSettingsPage>(new ExtensionSettingsPage());
    page->m_extensions = extensions;
    page->m_mcp = mcp;
    page->m_on_changed = std::move(on_changed);
    return page;
}

void ExtensionSettingsPage::populate(peel::Adw::PreferencesGroup &group)
{
    auto heading = Gtk::Label::create(_("Extensions"));
    heading->set_xalign(0.0f);
    heading->add_css_class("title-2");
    group.add(std::move(heading).release_floating_ptr());

    auto desc = Gtk::Label::create(
        _("Install extensions from GitHub to add MCP servers, skills, and commands."));
    desc->set_xalign(0.0f);
    desc->add_css_class("dim-label");
    desc->set_wrap(true);
    desc->set_margin_bottom(4);
    group.add(std::move(desc).release_floating_ptr());

    auto stack = Gtk::Stack::create();
    stack->set_vexpand(true);

    auto empty = Adw::StatusPage::create();
    empty->set_title(_("No Extensions Installed"));
    empty->set_description(
        _("Install extensions from GitHub to give the assistant new capabilities."));
    empty->add_css_class("compact");
    stack->add_named(std::move(empty).release_floating_ptr(), "empty");

    auto list = Gtk::ListBox::create();
    list->add_css_class("boxed-list");
    list->set_selection_mode(Gtk::SelectionMode::NONE);
    m_list = static_cast<Gtk::ListBox *>(list);
    stack->add_named(std::move(list).release_floating_ptr(), "list");
    m_stack = static_cast<Gtk::Stack *>(stack);
    group.add(std::move(stack).release_floating_ptr());

    auto button_bar = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 8);
    button_bar->set_halign(Gtk::Align::START);

    auto *self = this;

    auto install_btn = Gtk::Button::create_with_label(_("Install from GitHub"));
    install_btn->add_css_class("suggested-action");
    install_btn->connect_clicked([self](Gtk::Button *) { self->onInstallFromUrl(); });
    button_bar->append(std::move(install_btn));

    group.add(std::move(button_bar).release_floating_ptr());

    refreshList();
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

    auto *self = this;

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
            auto server_count = static_cast<unsigned long>(ext.mcp_servers.size());
            g_autofree gchar *mcp_sub = g_strdup_printf(
                dngettext(GETTEXT_PACKAGE,
                    "%lu server provided",
                    "%lu servers provided",
                    server_count),
                server_count);
            mcp_label->set_subtitle(mcp_sub);
            row->add_row(std::move(mcp_label).release_floating_ptr());

            for (auto& srv : ext.mcp_servers) {
                auto srv_row = Adw::ActionRow::create();
                auto srv_title = srv.id;
                srv_row->set_title(srv_title.c_str());

                std::string srv_subtitle;
                if (!srv.url.empty()) {
                    srv_subtitle = "http " "\302\267" " " + srv.url;
                } else if (!srv.command.empty()) {
                    srv_subtitle = "stdio " "\302\267" " " + srv.command;
                    for (auto& a : srv.args) srv_subtitle += " " + a;
                }
                srv_row->set_subtitle(srv_subtitle.c_str());
                srv_row->set_use_markup(false);

                auto state_lbl = Gtk::Label::create(
                    srv.enabled ? _("enabled") : _("disabled"));
                state_lbl->add_css_class(
                    srv.enabled ? "success" : "dim-label");
                srv_row->add_suffix(std::move(state_lbl).release_floating_ptr());

                row->add_row(std::move(srv_row).release_floating_ptr());
            }
        }

        if (!ext.skills.empty()) {
            auto skill_label = Adw::ActionRow::create();
            skill_label->set_title(_("Skills"));
            auto skill_count = static_cast<unsigned long>(ext.skills.size());
            g_autofree gchar *skill_sub = g_strdup_printf(
                dngettext(GETTEXT_PACKAGE,
                    "%lu skill provided",
                    "%lu skills provided",
                    skill_count),
                skill_count);
            skill_label->set_subtitle(skill_sub);
            row->add_row(std::move(skill_label).release_floating_ptr());
        }

        if (!ext.commands.empty()) {
            auto cmd_label = Adw::ActionRow::create();
            cmd_label->set_title(_("Commands"));
            auto cmd_count = static_cast<unsigned long>(ext.commands.size());
            g_autofree gchar *cmd_sub = g_strdup_printf(
                dngettext(GETTEXT_PACKAGE,
                    "%lu command",
                    "%lu commands",
                    cmd_count),
                cmd_count);
            cmd_label->set_subtitle(cmd_sub);
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
        uninstall_btn->connect_clicked([self, name](Gtk::Button *) {
            if (self->m_extensions->uninstall(name)) {
                self->m_mcp->stopAll();
                self->m_mcp->startAll();
                self->refreshList();
                self->m_on_changed();
            }
        });
        row->add_suffix(std::move(uninstall_btn).release_floating_ptr());

        auto update_btn = Gtk::Button::create_with_label(_("Update"));
        update_btn->connect_clicked([self, name](Gtk::Button *) {
            if (self->m_extensions->update(name)) {
                self->m_mcp->stopAll();
                self->m_mcp->startAll();
                self->refreshList();
                self->m_on_changed();
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
    auto *self = this;

    raw_dlg->connect_done([raw_dlg, self](ExtensionInstallDialog *) {
        if (!raw_dlg->wasAccepted()) return;

        auto url = raw_dlg->url();

        if (self->m_extensions->install(url)) {
            if (self->m_mcp) {
                self->m_mcp->stopAll();
                self->m_mcp->startAll();
            }
            self->refreshList();
            self->m_on_changed();
        } else {
            spdlog::warn("ExtensionSettingsPage: install failed for {}", url);
        }
        raw_dlg->close();
    });
    raw_dlg->present();
}

} // namespace rook::gui
