#pragma once

#include <peel/Adw/Adw.h>
#include <peel/Gtk/Gtk.h>
#include <peel/Gio/Gio.h>
#include <peel/GLib/functions.h>
#include <peel/class.h>
#include <memory>
#include <map>
#include <string_view>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/conversation.hpp"
#include "rook/core/domain_actor.hpp"
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"
#include "rook/adapters/secret_store.hpp"
#include "rook/core/settings.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/mcp/mcp_client_adapter.hpp"
#include "rook/adapters/security/security_manager.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/ports/tool_port.hpp"
#include "rook/adapters/hook/plugin_loader.hpp"

namespace rook::ports {
class WakewordPort;
class SpeechToTextPort;
class TextToSpeechPort;
class AudioDevicePort;
}

namespace rook::gui {

class RookWindow;
class TrayIcon;

class RookApplication final : public peel::Adw::Application
{
    PEEL_SIMPLE_CLASS(RookApplication, peel::Adw::Application)
    friend class peel::Gio::Application;

    inline void init(Class *);
    inline void vfunc_activate ();
    inline void vfunc_dispose ();

    void loadConfig();
    void saveConfig();
    void loadHookPlugins();
    void deactivateExtensionHooks(std::string_view name);
    void startModelDiscovery(RookWindow &window);
    void onFirstRunDone(RookWindow *window);

    std::string m_data_dir;
    rook::domain::EventBus m_bus;
    std::unique_ptr<rook::ports::LlmPort> m_llm;
    std::unique_ptr<rook::ports::StorePort> m_store;
    std::unique_ptr<rook::adapters::SecretStore> m_secrets;
    rook::core::SettingsLoader m_settings;
    std::unique_ptr<rook::core::DomainActor> m_actor;
    std::unique_ptr<rook::ports::ToolPort> m_tool_port;
    std::unique_ptr<rook::adapters::mcp::McpServerManager> m_mcp_manager;
    std::unique_ptr<rook::adapters::security::SecurityManager> m_security;
    std::unique_ptr<rook::adapters::extension::ExtensionManager> m_extensions;
    std::vector<rook::adapters::extension::CustomSkill> m_custom_skills;
    std::unique_ptr<rook::ports::ToolPermissionPort> m_permission_port;
    std::unique_ptr<rook::ports::WakewordPort> m_wakeword;
    std::unique_ptr<rook::ports::SpeechToTextPort> m_stt;
    std::unique_ptr<rook::ports::TextToSpeechPort> m_tts;
    std::unique_ptr<rook::ports::AudioDevicePort> m_audio_device;
    bool m_first_run = true;
    bool m_css_loaded = false;
    std::unique_ptr<TrayIcon> m_tray_icon;
    rook::adapters::hook::PluginLoader m_plugin_loader;
    std::map<std::string, std::vector<std::string>, std::less<>> m_ext_hook_ids;
    std::string m_plugin_dir;
    RookWindow *m_window = nullptr;

public:
    static peel::RefPtr<RookApplication> create();

    rook::domain::EventBus& eventBus() { return m_bus; }
    rook::adapters::mcp::McpServerManager* mcpManager() { return m_mcp_manager.get(); }
    rook::adapters::security::SecurityManager* security() { return m_security.get(); }
    rook::adapters::extension::ExtensionManager* extensions() { return m_extensions.get(); }
    std::vector<rook::adapters::extension::CustomSkill>& customSkills() { return m_custom_skills; }
    rook::ports::WakewordPort* wakeword() { return m_wakeword.get(); }
    rook::ports::SpeechToTextPort* stt() { return m_stt.get(); }
    rook::ports::TextToSpeechPort* tts() { return m_tts.get(); }
    rook::ports::AudioDevicePort* audioDevice() { return m_audio_device.get(); }
};

} // namespace rook::gui
