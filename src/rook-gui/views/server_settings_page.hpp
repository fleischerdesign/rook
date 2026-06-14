#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <functional>
#include <memory>
#include "rook/core/peer_manager.hpp"

namespace rook::gui {

class ServerSettingsPage {
public:
    using ChangeFn = std::function<void()>;

    static std::unique_ptr<ServerSettingsPage> create(
        rook::core::PeerManager& peers, ChangeFn on_changed);

    void populate(peel::Adw::PreferencesGroup& sync_group,
                  peel::Adw::PreferencesGroup& peers_group);

private:
    ServerSettingsPage() = default;

    void refreshList();
    void onAddClicked();
    void onConnectClicked(const std::string& address);
    void onDisconnectClicked(const std::string& address);
    void onRemoveClicked(const std::string& address);

    rook::core::PeerManager* m_peers = nullptr;
    peel::Gtk::ListBox* m_peer_list = nullptr;
    peel::Gtk::ListBox* m_sync_list = nullptr;
    ChangeFn m_on_changed;
};

} // namespace rook::gui
