#pragma once

#include <memory>
#include <functional>
#include <string>

namespace rook::gui {

class TrayIcon {
public:
    using Callback = std::function<void()>;

    struct Impl;

    TrayIcon(std::string service_name, std::string title, std::string icon_name);
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    void show();
    void hide();

    void onActivate(Callback cb) { m_on_activate = std::move(cb); }
    void onSecondaryActivate(Callback cb) { m_on_secondary = std::move(cb); }

private:
    std::unique_ptr<Impl> m_impl;

    Callback m_on_activate;
    Callback m_on_secondary;
};

} // namespace rook::gui
