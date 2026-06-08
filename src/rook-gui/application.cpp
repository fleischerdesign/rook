#include "application.hpp"
#include <glib/gi18n.h>
#include "window.hpp"
#include "views/first_run_wizard.hpp"
#include "views/tray_icon.hpp"
#include "views/permission_port.hpp"
#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/store/json_store.hpp"
#include "rook/adapters/model/model_cache.hpp"
#include "rook/adapters/model/model_discovery_factory.hpp"
#include "rook/adapters/mcp/null_tool_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/mcp/mcp_client_adapter.hpp"
#include "rook/adapters/mcp/stdio_transport.hpp"
#include "rook/adapters/builtin/builtin_tool_port.hpp"
#include "rook/adapters/composite/composite_tool_port.hpp"
#include "rook/adapters/security/security_guarded_tool_port.hpp"
#include "rook/adapters/security/security_manager.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include "rook/adapters/hook/builtin_hooks.hpp"
#include "rook/adapters/hook/core_api_provider.hpp"
#include "rook/adapters/audio/miniaudio_adapter.hpp"
#include "rook/adapters/audio/openwakeword_adapter.hpp"
#include "rook/adapters/audio/whisper_adapter.hpp"
#include "rook/adapters/audio/piper_adapter.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <future>

using namespace peel;

namespace rook::gui {

PEEL_CLASS_IMPL(RookApplication, "RookApplication", Adw::Application)

inline void RookApplication::Class::init()
{
    override_vfunc_activate<RookApplication>();
    override_vfunc_dispose<RookApplication>();
}

inline void RookApplication::init(Class *)
{
    new (&m_bus) rook::domain::EventBus();

    m_data_dir = GLib::get_user_data_dir() + std::string("/rook");

    m_store = rook::adapters::store::makeJsonStore(m_data_dir);
    m_secrets = std::make_unique<rook::adapters::SecretStore>(
        "io.github.fleischerdesign.Rook");

    m_llm = rook::adapters::llm::makeMultiProviderAdapter();

    m_security = std::make_unique<rook::adapters::security::SecurityManager>();

    m_mcp_manager = std::make_unique<rook::adapters::mcp::McpServerManager>();

    auto composite = std::make_unique<rook::adapters::composite::CompositeToolPort>();
    auto guarded_builtin =
        std::make_unique<rook::adapters::security::SecurityGuardedToolPort>(
            *m_security,
            rook::adapters::builtin::makeBuiltinToolPort(),
            "builtin");
    composite->addPort(std::move(guarded_builtin));

    m_extensions = std::make_unique<rook::adapters::extension::ExtensionManager>(
        m_data_dir, m_mcp_manager.get());

    auto config_json = m_store->loadConfig();
    if (!config_json.empty() && config_json != "{}") {
        try {
            auto j = nlohmann::json::parse(config_json);
            if (j.contains("mcp_servers") && j["mcp_servers"].is_array()) {
                for (auto& s : j["mcp_servers"]) {
                    rook::adapters::mcp::McpServerConfig cfg;
                    cfg.id = s.value("id", "");
                    cfg.enabled = s.value("enabled", true);
                    cfg.source = s.value("source", "manual");
                    if (!cfg.source.empty() && cfg.source != "manual") continue;

                    auto ttype = s.value("type", "stdio");

                    if (ttype == "http_sse") {
                        cfg.transport_type =
                            rook::adapters::mcp::McpTransportType::HttpSse;
                        cfg.url = s.value("url", "");
                        if (s.contains("headers") && s["headers"].is_object()) {
                            for (auto& [k, v] : s["headers"].items()) {
                                if (v.is_string())
                                    cfg.headers.emplace_back(k, v.get<std::string>());
                            }
                        }
                    } else {
                        cfg.transport_type =
                            rook::adapters::mcp::McpTransportType::Stdio;
                        cfg.command = s.value("command", "");
                        if (s.contains("args") && s["args"].is_array()) {
                            for (auto& a : s["args"])
                                cfg.args.push_back(a.get<std::string>());
                        }
                    }

                    bool is_stdio = cfg.transport_type ==
                        rook::adapters::mcp::McpTransportType::Stdio;
                    bool is_http = cfg.transport_type ==
                        rook::adapters::mcp::McpTransportType::HttpSse;

                    bool valid = !cfg.id.empty() &&
                        ((is_stdio && !cfg.command.empty()) ||
                         (is_http && !cfg.url.empty()));

                    if (valid) {
                        m_mcp_manager->addServer(std::move(cfg));
                    }
                }
            }
            if (j.contains("mcp_servers")) {
                m_security->loadFromConfig(
                    j["mcp_servers"].dump());
            }
            if (j.contains("extensions")) {
                m_extensions->loadFromConfig(
                    j["extensions"].dump());
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse MCP config: {}", e.what());
        }
    }

    auto config_json2 = m_store->loadConfig();
    if (!config_json2.empty() && config_json2 != "{}") {
        try {
            auto j = nlohmann::json::parse(config_json2);
            if (j.contains("custom_skills") && j["custom_skills"].is_array()) {
                for (auto& s : j["custom_skills"]) {
                    rook::adapters::extension::CustomSkill skill;
                    skill.name = s.value("name", "");
                    skill.description = s.value("description", "");
                    skill.prompt = s.value("prompt", "");
                    skill.enabled = s.value("enabled", false);
                    if (!skill.name.empty() && !skill.prompt.empty())
                        m_custom_skills.push_back(std::move(skill));
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse custom skills: {}", e.what());
        }
    }

    if (m_mcp_manager->serverCount() > 0) {
        m_mcp_manager->startAll();
        m_mcp_manager->setSecurityPort(m_security.get());
        composite->addPort(rook::adapters::mcp::makeMcpToolPort(*m_mcp_manager));
    }

    m_tool_port = std::move(composite);

    m_first_run = !m_settings.load(*m_store, *m_llm, *m_secrets);

    m_actor = std::make_unique<rook::core::DomainActor>();

    auto permission_port = std::make_unique<GtkPermissionPort>(*m_actor);
    auto *perm_raw = permission_port.get();
    m_permission_port = std::move(permission_port);

    m_actor->start(*m_llm, *m_tool_port, perm_raw, m_store.get(),
        [this](rook::domain::DomainEvent event) {
            GLib::idle_add_once([this, ev = std::move(event)]() {
                m_bus.publish(ev);
            });
        },
        m_extensions.get(), &m_custom_skills);

    loadHookPlugins();

    m_audio_device = std::make_unique<rook::adapters::audio::MiniaudioAdapter>();
    m_wakeword = std::make_unique<rook::adapters::audio::OpenWakeWordAdapter>();
    m_stt = std::make_unique<rook::adapters::audio::WhisperAdapter>();
    m_tts = std::make_unique<rook::adapters::audio::PiperAdapter>();
    m_actor->setupAudio(*m_wakeword, *m_stt, *m_tts, *m_audio_device);

    if (m_wakeword->isReady())
        SPDLOG_INFO("wakeword engine ready: {}", m_wakeword->engineName());
    if (m_stt->isReady())
        SPDLOG_INFO("STT engine ready: {}", m_stt->engineName());
    if (m_tts->isReady())
        SPDLOG_INFO("TTS engine ready: {}", m_tts->engineName());

    auto prefs_action = Gio::SimpleAction::create("preferences", nullptr);
    prefs_action->connect_activate(
        [this](Gio::SimpleAction *, GLib::Variant *) {
            if (auto *w = get_active_window())
                if (auto *rw = w->template cast<RookWindow>())
                    rw->showPreferences();
        });
    cast<Gio::ActionMap>()->add_action(prefs_action);

    auto about_action = Gio::SimpleAction::create("about", nullptr);
    about_action->connect_activate(
        [this](Gio::SimpleAction *, GLib::Variant *) {
            if (auto *w = get_active_window())
                if (auto *rw = w->template cast<RookWindow>())
                    rw->showAbout();
        });
    cast<Gio::ActionMap>()->add_action(about_action);
}

RefPtr<RookApplication> RookApplication::create()
{
    return Object::create<RookApplication>(
        prop_application_id(), "io.github.fleischerdesign.Rook",
        prop_flags(), Gio::Application::Flags::DEFAULT_FLAGS);
}

inline void RookApplication::vfunc_activate()
{
    parent_vfunc_activate<RookApplication>();

    if (m_window) {
        gtk_window_present(GTK_WINDOW(reinterpret_cast<::GObject*>(m_window)));
        return;
    }

    if (!m_css_loaded) {
        auto* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_string(provider,
            ".code-block frame {"
            "  background-color: alpha(currentColor, 0.06);"
            "  border-radius: 6px;"
            "}"
            ".code-block textview {"
            "  font-family: monospace;"
            "}"
            ".code-block textview text {"
            "  background-color: transparent;"
            "}"
            ".blockquote {"
            "  border-left: 3px solid alpha(currentColor, 0.2);"
            "  padding-left: 8px;"
            "  opacity: 0.85;"
            "}"
            ".separator-row {"
            "  padding: 0;"
            "  min-height: 0;"
            "  background: transparent;"
            "  box-shadow: none;"
            "}");
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);
        m_css_loaded = true;
    }

    auto *window = RookWindow::create(this, m_actor.get(), m_bus, *m_llm,
        m_mcp_manager.get(), m_security.get(), m_extensions.get(),
        &m_custom_skills,
        [this]() { saveConfig(); },
        [this](std::string_view name) { deactivateExtensionHooks(name); },
        m_wakeword.get(), m_stt.get(), m_tts.get(), m_audio_device.get(),
        m_permission_port.get());
    m_window = window;
    window->present();

    if (!m_tray_icon) {
        auto tray_name = "org.kde.StatusNotifierItem-"
            + std::to_string(getpid()) + "-rook";
        m_tray_icon = std::make_unique<TrayIcon>(
            tray_name, _("Rook"), "io.github.fleischerdesign.Rook");

        m_tray_icon->onActivate([this]() {
            if (m_window)
                gtk_window_present(GTK_WINDOW(
                    reinterpret_cast<::GObject*>(m_window)));
        });

        m_tray_icon->show();

        g_signal_connect(reinterpret_cast<::GObject*>(window), "close-request",
            G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
                gtk_widget_set_visible(GTK_WIDGET(data), FALSE);
                return TRUE;
            }), window);
    }

    if (m_first_run) {
        auto wizard = FirstRunWizard::create();
        FirstRunWizard *raw_wiz = wizard;
        raw_wiz->connect_done([this, raw_wiz, window](FirstRunWizard *) {
            auto cfg = raw_wiz->getConfig();
            auto info = rook::ports::ProviderRegistry::instance().find(cfg.provider);

            auto new_provider = rook::ports::LlmProviderConfig{
                .id = "",
                .display_name = info ? info->display_name : cfg.provider,
                .type = cfg.provider,
                .base_url = info ? info->base_url : "",
                .api_key = cfg.api_key,
                .default_model = info ? info->default_model : "",
                .enabled = true,
            };

            m_llm->addProvider(new_provider);
            m_first_run = false;
            saveConfig();
            startModelDiscovery(*window);
            raw_wiz->close();
        });
        raw_wiz->connect_hide([raw_wiz](Gtk::Widget *) { g_object_unref(raw_wiz); });
        raw_wiz->present();
    }

    startModelDiscovery(*window);
}

inline void RookApplication::vfunc_dispose()
{
    m_tray_icon.reset();
    if (m_actor) m_actor->stop();
    if (m_mcp_manager) m_mcp_manager->stopAll();
    m_llm.reset();
    m_store.reset();
    m_secrets.reset();
    m_permission_port.reset();
    m_actor.reset();
    m_mcp_manager.reset();
    parent_vfunc_dispose<RookApplication>();
    m_bus.~EventBus();
}

void RookApplication::startModelDiscovery(RookWindow &window)
{
    auto providers = m_llm->listProviders();
    RookWindow *win = &window;

    for (const auto &prov : providers) {
        if (!prov.enabled) continue;

        auto api_key = prov.api_key;
        auto base_url = prov.base_url;
        auto prov_type = prov.type;
        auto prov_id = prov.id;

        (void)std::async(std::launch::async, [prov_type, prov_id, api_key, base_url, win]() {
            std::unique_ptr<rook::ports::ModelDiscoveryPort> discovery;

            if (prov_type == "ollama") {
                discovery = rook::adapters::model::makeOllamaDiscovery(base_url);
            } else if (prov_type == "anthropic") {
                discovery = rook::adapters::model::makeAnthropicDiscovery();
            } else {
                discovery = rook::adapters::model::makeOpenAiDiscovery(base_url);
            }

            auto models = discovery->fetchModels(api_key);
            rook::adapters::model::ModelCache::instance().store(prov_id, std::move(models));

            GLib::idle_add_once([win]() {
                win->refreshModels();
            });
        });
    }
}

void RookApplication::loadHookPlugins()
{
    m_plugin_dir = GLib::get_user_config_dir() + std::string("/rook/plugins/hook");

    if (g_mkdir_with_parents(m_plugin_dir.c_str(), 0755) == 0) {
        spdlog::info("Plugin directory ready: {}", m_plugin_dir);
    } else {
        spdlog::warn("Cannot create plugin directory: {}", m_plugin_dir);
    }

    auto config_json = m_store->loadConfig();
    auto core_api = rook::adapters::hook::makeCoreAPI(
        config_json.empty() ? "{}" : config_json);

    auto standalone = m_plugin_loader.loadFromDirectory(m_plugin_dir, core_api);
    int loaded = 0;

    for (auto& hook : standalone) {
        if (!m_actor->hooks().contains(hook->id())) {
            m_actor->hooks().registerHook(std::move(hook));
            loaded++;
        }
    }

    if (m_extensions) {
        for (auto& ext : m_extensions->listInstalled()) {
            for (auto& plugin_path : ext.plugin_paths) {
                auto full = ext.install_path + "/" + plugin_path;
                auto ext_hooks = m_plugin_loader.loadFromDirectory(
                    full, core_api);
                for (auto& hook : ext_hooks) {
                    auto id = hook->id();
                    m_ext_hook_ids[std::string(ext.name)].push_back(id);
                    if (!m_actor->hooks().contains(id)) {
                        m_actor->hooks().registerHook(std::move(hook));
                        loaded++;
                    }
                }
            }
        }
    }

    if (loaded > 0)
        spdlog::info("Loaded {} new hook plugin(s)", loaded);

    if (!m_actor->hooks().contains("builtin.extension_context")) {
        m_actor->hooks().registerHook(
            rook::adapters::hook::makeExtensionContextHook(
                m_extensions.get(), &m_custom_skills));
    }

    if (!m_actor->hooks().contains("builtin.response_cleanup")) {
        m_actor->hooks().registerHook(
            rook::adapters::hook::makeResponseCleanupHook());
    }
}

void RookApplication::deactivateExtensionHooks(std::string_view name)
{
    auto it = m_ext_hook_ids.find(std::string(name));
    if (it == m_ext_hook_ids.end()) return;

    for (auto& id : it->second) {
        m_actor->hooks().deactivateByPrefix(id);
    }
    m_ext_hook_ids.erase(it);
    spdlog::info("Deactivated hooks for extension '{}'", name);
}

void RookApplication::saveConfig()
{
    m_settings.save(*m_store, *m_llm, *m_secrets);

    auto config_json = m_store->loadConfig();
    nlohmann::json j;
    if (!config_json.empty() && config_json != "{}") {
        try {
            j = nlohmann::json::parse(config_json);
        } catch (...) {}
    }

    nlohmann::json mcp_array = nlohmann::json::array();
    if (m_mcp_manager) {
        auto servers = m_mcp_manager->listServers();
        for (const auto& srv : servers) {
            bool is_manual = srv.source.empty() || srv.source == "manual";
            bool has_caps = m_security &&
                            m_security->findCapability(srv.id) != nullptr;

            if (!is_manual && !has_caps) continue;

            nlohmann::json s;
            s["id"] = srv.id;
            s["enabled"] = srv.enabled;
            s["source"] = srv.source;

            if (is_manual) {
                if (srv.transport_type ==
                    rook::adapters::mcp::McpTransportType::HttpSse)
                {
                    s["type"] = "http_sse";
                    s["url"] = srv.url;
                    nlohmann::json hdrs = nlohmann::json::object();
                    for (auto& [k, v] : srv.headers) {
                        hdrs[k] = v;
                    }
                    s["headers"] = hdrs;
                } else {
                    s["type"] = "stdio";
                    s["command"] = srv.command;
                    s["args"] = srv.args;
                }
            }

            if (has_caps) {
                auto* cap = m_security->findCapability(srv.id);
                nlohmann::json caps;
                caps["read"] = cap->readPaths();
                caps["write"] = cap->writePaths();
                caps["network"] = cap->allowsNetwork();
                caps["max_memory_mb"] = cap->maxMemoryMb();
                caps["max_cpu_time_secs"] = cap->maxCpuTimeSecs();
                s["capabilities"] = caps;
            }

            mcp_array.push_back(s);
        }
    }
    j["mcp_servers"] = mcp_array;

    nlohmann::json custom_skills_arr = nlohmann::json::array();
    for (auto& skill : m_custom_skills) {
        nlohmann::json s;
        s["name"] = skill.name;
        s["description"] = skill.description;
        s["prompt"] = skill.prompt;
        s["enabled"] = skill.enabled;
        custom_skills_arr.push_back(s);
    }
    j["custom_skills"] = custom_skills_arr;

    if (m_extensions) {
        auto ext_json = m_extensions->saveToConfig();
        if (!ext_json.empty()) {
            try {
                j["extensions"] = nlohmann::json::parse(ext_json);
            } catch (...) {}
        }
    }

    m_store->saveConfig(j.dump(2));
    spdlog::info("Config saved to {}", m_data_dir);

    loadHookPlugins();
}

} // namespace rook::gui
