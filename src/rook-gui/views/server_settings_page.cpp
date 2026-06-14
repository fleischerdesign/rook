#include <glib/gi18n.h>
#include "server_settings_page.hpp"
#include <spdlog/spdlog.h>

using namespace peel;

namespace rook::gui {

std::unique_ptr<ServerSettingsPage> ServerSettingsPage::create(
    rook::core::PeerManager& peers, ChangeFn on_changed)
{
    auto page = std::unique_ptr<ServerSettingsPage>(new ServerSettingsPage());
    page->m_peers = &peers;
    page->m_on_changed = std::move(on_changed);
    return page;
}

void ServerSettingsPage::populate(
    peel::Adw::PreferencesGroup& sync_group,
    peel::Adw::PreferencesGroup& peers_group)
{
    {
        auto heading = Gtk::Label::create(_("Synchronisation"));
        heading->set_xalign(0.0f);
        heading->add_css_class("title-2");
        sync_group.add(std::move(heading).release_floating_ptr());

        auto desc = Gtk::Label::create(
            _("When enabled, settings, extensions, and optionally "
              "chat history are synchronised with your connected devices."));
        desc->set_xalign(0.0f);
        desc->set_wrap(true);
        desc->add_css_class("dim-label");
        desc->set_margin_bottom(12);
        sync_group.add(std::move(desc).release_floating_ptr());

        auto sync_list = Gtk::ListBox::create();
        sync_list->set_hexpand(true);
        sync_list->add_css_class("boxed-list");
        m_sync_list = sync_list;
        sync_group.add(std::move(sync_list).release_floating_ptr());
    }

    {
        auto heading = Gtk::Label::create(_("Devices"));
        heading->set_xalign(0.0f);
        heading->add_css_class("title-2");
        peers_group.add(std::move(heading).release_floating_ptr());

        auto desc = Gtk::Label::create(
            _("Connect to other Rook instances for synchronisation "
              "and remote LLM execution."));
        desc->set_xalign(0.0f);
        desc->set_wrap(true);
        desc->add_css_class("dim-label");
        desc->set_margin_bottom(12);
        peers_group.add(std::move(desc).release_floating_ptr());

        auto list = Gtk::ListBox::create();
        list->set_hexpand(true);
        list->set_vexpand(true);
        list->add_css_class("boxed-list");
        m_peer_list = list;
        peers_group.add(std::move(list).release_floating_ptr());

        auto add_btn = Gtk::Button::create_with_label(_("Add Device"));
        add_btn->add_css_class("suggested-action");
        add_btn->set_halign(Gtk::Align::START);
        add_btn->set_margin_top(12);
        auto* self = this;
        add_btn->connect_clicked([self](Gtk::Button*) {
            self->onAddClicked();
        });
        peers_group.add(std::move(add_btn).release_floating_ptr());
    }

    refreshList();
}

void ServerSettingsPage::refreshList()
{
    while (auto* row = m_peer_list->get_row_at_index(0))
        m_peer_list->remove(row);

    {
        while (auto* row = m_sync_list->get_row_at_index(0))
            m_sync_list->remove(row);

        auto settings_row = Adw::SwitchRow::create();
        settings_row->set_title(_("Sync Settings"));
        settings_row->set_subtitle(_("Synchronise preferences across devices"));
        m_sync_list->append(std::move(settings_row).release_floating_ptr());

        auto ext_row = Adw::SwitchRow::create();
        ext_row->set_title(_("Sync Extensions"));
        ext_row->set_subtitle(_("Synchronise installed extensions"));
        ext_row->set_active(true);
        m_sync_list->append(std::move(ext_row).release_floating_ptr());

        auto chat_row = Adw::SwitchRow::create();
        chat_row->set_title(_("Sync Chat History"));
        chat_row->set_subtitle(_("Synchronise conversation history"));
        chat_row->set_active(false);
        m_sync_list->append(std::move(chat_row).release_floating_ptr());
    }

    auto local_row = Adw::ExpanderRow::create();
    local_row->set_title(_("This Computer"));
    local_row->set_subtitle(_("Local — LLM runs here"));

    auto local_llm = Adw::ActionRow::create();
    local_llm->set_title(_("LLM Execution"));
    local_llm->set_subtitle(_("Running locally"));
    local_row->add_row(std::move(local_llm).release_floating_ptr());

    m_peer_list->append(std::move(local_row).release_floating_ptr());

    auto peers = m_peers->peers();
    for (const auto* peer : peers) {
        auto row = Adw::ExpanderRow::create();
        row->set_title(std::string(peer->config.address).c_str());
        row->set_subtitle(peer->connected
            ? _("Connected")
            : _("Disconnected"));

        if (!peer->config.token.empty()) {
            auto icon = Gtk::Image::create_from_icon_name(
                "changes-prevent-symbolic");
            row->add_prefix(std::move(icon).release_floating_ptr());
        }

        auto status_row = Adw::ActionRow::create();
        status_row->set_title(_("Status"));
        status_row->set_subtitle(peer->connected
            ? _("Connected")
            : _("Not connected"));
        row->add_row(std::move(status_row).release_floating_ptr());

        for (const auto& sync_cfg : {
            std::pair{_("Sync Settings"), peer->config.sync_settings},
            std::pair{_("Sync Extensions"), peer->config.sync_extensions},
            std::pair{_("Sync Chats"), peer->config.sync_chats},
        }) {
            auto toggle = Adw::SwitchRow::create();
            toggle->set_title(sync_cfg.first);
            toggle->set_active(sync_cfg.second);
            toggle->connect_notify(Adw::SwitchRow::prop_active(),
                [self = this, addr = std::string(peer->config.address)]
                (GObject::Object*, GObject::ParamSpec*) mutable {
                    SPDLOG_DEBUG("Toggle changed for peer {}", addr);
                });
            row->add_row(std::move(toggle).release_floating_ptr());
        }

        auto* self = this;
        std::string addr = peer->config.address;

        auto buttons_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
        buttons_box->set_margin_top(6);

        if (peer->connected) {
            auto disc_btn = Gtk::Button::create_with_label(_("Disconnect"));
            disc_btn->add_css_class("destructive-action");
            disc_btn->connect_clicked(
                [self, addr](Gtk::Button*) {
                    self->onDisconnectClicked(addr);
                });
            buttons_box->append(std::move(disc_btn).release_floating_ptr());
        } else {
            auto conn_btn = Gtk::Button::create_with_label(_("Connect"));
            conn_btn->add_css_class("suggested-action");
            conn_btn->connect_clicked(
                [self, addr](Gtk::Button*) {
                    self->onConnectClicked(addr);
                });
            buttons_box->append(std::move(conn_btn).release_floating_ptr());
        }

        auto remove_btn = Gtk::Button::create_with_label(_("Remove"));
        remove_btn->connect_clicked(
            [self, addr](Gtk::Button*) {
                self->onRemoveClicked(addr);
            });
        buttons_box->append(std::move(remove_btn).release_floating_ptr());

        auto btn_row = Adw::ActionRow::create();
        btn_row->set_child(std::move(buttons_box).release_floating_ptr());
        row->add_row(std::move(btn_row).release_floating_ptr());

        m_peer_list->append(std::move(row).release_floating_ptr());
    }
}

void ServerSettingsPage::onAddClicked()
{
    auto dialog = Gtk::Dialog::create();
    dialog->set_title(_("Add Device"));
    dialog->set_modal(true);
    dialog->set_default_size(400, -1);

    auto content = dialog->get_content_area();
    content->set_margin_start(24);
    content->set_margin_end(24);
    content->set_margin_top(24);
    content->set_margin_bottom(12);
    content->set_spacing(12);

    auto label = Gtk::Label::create(
        _("Enter the address of the Rook instance to connect to."));
    label->set_xalign(0.0f);
    label->set_wrap(true);
    content->append(std::move(label).release_floating_ptr());

    auto entry = Gtk::Entry::create();
    auto* entry_raw = entry.operator->();
    entry->set_placeholder_text(_("hostname:port"));
    content->append(std::move(entry).release_floating_ptr());

    auto token_entry = Gtk::Entry::create();
    auto* token_raw = token_entry.operator->();
    token_entry->set_placeholder_text(_("JWT token (optional)"));
    token_entry->set_visibility(false);
    content->append(std::move(token_entry).release_floating_ptr());

    dialog->add_button(_("Cancel"), 0);
    dialog->add_button(_("Connect"), 1);

    auto* self = this;
    dialog->connect_response(
        [self, entry_raw, token_raw]
        (Gtk::Dialog* dlg, int response) {
            (void)dlg;
            if (response != 1) return;
            std::string addr = entry_raw->get_text();
            std::string token = token_raw->get_text();
            if (addr.empty()) return;

            rook::core::PeerConfig cfg;
            cfg.address = addr;
            cfg.token = token;
            cfg.sync_settings = true;
            cfg.sync_extensions = true;
            cfg.sync_chats = false;

            self->m_peers->addPeer(std::move(cfg));
            self->m_peers->connect(addr);
            self->refreshList();
            if (self->m_on_changed) self->m_on_changed();
        });

    dialog->present();
}

void ServerSettingsPage::onConnectClicked(const std::string& address)
{
    m_peers->connect(address);
    refreshList();
    if (m_on_changed) m_on_changed();
}

void ServerSettingsPage::onDisconnectClicked(const std::string& address)
{
    m_peers->disconnect(address);
    refreshList();
    if (m_on_changed) m_on_changed();
}

void ServerSettingsPage::onRemoveClicked(const std::string& address)
{
    m_peers->disconnect(address);
    m_peers->removePeer(address);
    refreshList();
    if (m_on_changed) m_on_changed();
}
} // namespace rook::gui
