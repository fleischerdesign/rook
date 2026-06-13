#include <memory>
#include <string>
#include <csignal>
#include <future>

#include <grpcpp/server_builder.h>
#include <grpcpp/server.h>

#include "rook/core/domain_actor.hpp"
#include "rook/core/settings.hpp"
#include "rook/adapters/server/grpc_service_adapter.hpp"
#include "rook/adapters/server/tenant_manager.hpp"
#include "rook/adapters/server/tenant_service_adapter.hpp"
#include "rook/auth/jwt.hpp"
#include "rook/adapters/store/json_store.hpp"
#include "rook/adapters/secret_store.hpp"
#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/mcp/mcp_client_adapter.hpp"
#include "rook/adapters/mcp/null_tool_port.hpp"
#include "rook/adapters/builtin/builtin_tool_port.hpp"
#include "rook/adapters/composite/composite_tool_port.hpp"
#include "rook/adapters/security/security_manager.hpp"
#include "rook/adapters/security/security_guarded_tool_port.hpp"
#include "rook/adapters/extension/extension_manager.hpp"
#include "rook/adapters/hook/builtin_hooks.hpp"
#include "rook/adapters/hook/core_api_provider.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fstream>
#include <sstream>

namespace {

std::atomic<bool> g_shutdown{false};

void handleSignal(int)
{
    g_shutdown.store(true, std::memory_order_release);
}

std::string defaultDataDir()
{
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
        return std::string(xdg) + "/rook";
    const char* home = std::getenv("HOME");
    if (home && *home)
        return std::string(home) + "/.local/share/rook";
    return "/var/lib/rook";
}

std::string defaultConfigDir()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return std::string(xdg) + "/rook";
    const char* home = std::getenv("HOME");
    if (home && *home)
        return std::string(home) + "/.config/rook";
    return "/etc/rook";
}

struct RookdConfig {
    std::string listen_address = "0.0.0.0:50051";
    std::string data_dir;
    std::string config_dir;
    std::string jwt_secret;
    std::string tenants_dir;
    std::string generate_token_for;
    bool verbose = false;
};

void parseArgs(int argc, char** argv, RookdConfig& cfg)
{
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            cfg.listen_address = std::string("0.0.0.0:") + argv[++i];
        } else if (arg == "--bind" && i + 1 < argc) {
            cfg.listen_address = argv[++i];
        } else if (arg == "--data-dir" && i + 1 < argc) {
            cfg.data_dir = argv[++i];
        } else if (arg == "--config-dir" && i + 1 < argc) {
            cfg.config_dir = argv[++i];
        } else if (arg == "--jwt-secret" && i + 1 < argc) {
            cfg.jwt_secret = argv[++i];
        } else if (arg == "--jwt-secret-file" && i + 1 < argc) {
            std::ifstream f(argv[++i]);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                cfg.jwt_secret = ss.str();
                while (!cfg.jwt_secret.empty()
                       && std::isspace(cfg.jwt_secret.back()))
                    cfg.jwt_secret.pop_back();
            }
        } else if (arg == "--tenants-dir" && i + 1 < argc) {
            cfg.tenants_dir = argv[++i];
        } else if (arg == "--generate-token" && i + 1 < argc) {
            cfg.generate_token_for = argv[++i];
        } else if (arg == "--verbose" || arg == "-v") {
            cfg.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            fmt::print(
                "rookd — Rook AI Agent Daemon\n\n"
                "Usage: rookd [options]\n\n"
                "Server:\n"
                "  --port PORT           gRPC port (default: 50051)\n"
                "  --bind ADDR:PORT      Full listen address (default: 0.0.0.0:50051)\n"
                "  --data-dir DIR        Data directory (default: $XDG_DATA_HOME/rook)\n"
                "  --config-dir DIR      Config directory (default: $XDG_CONFIG_HOME/rook)\n\n"
                "Multi-Tenancy:\n"
                "  --jwt-secret SECRET   Enable multi-tenant mode with this HMAC secret\n"
                "  --jwt-secret-file F   Read JWT secret from file\n"
                "  --tenants-dir DIR     Tenant data root (default: data-dir)\n"
                "  --generate-token ID   Generate a JWT token for tenant ID and exit\n\n"
                "Logging:\n"
                "  --verbose, -v         Enable debug logging\n"
                "  --help, -h            Show this help\n"
            );
            std::exit(0);
        }
    }

    if (cfg.data_dir.empty())
        cfg.data_dir = defaultDataDir();
    if (cfg.config_dir.empty())
        cfg.config_dir = defaultConfigDir();
    if (cfg.tenants_dir.empty())
        cfg.tenants_dir = cfg.data_dir;
}

void setupLogging(bool verbose)
{
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    auto logger = std::make_shared<spdlog::logger>("rookd", std::move(sink));
    logger->set_level(verbose ? spdlog::level::debug : spdlog::level::info);
    spdlog::set_default_logger(std::move(logger));
}

void loadMcpServers(rook::adapters::mcp::McpServerManager& mgr,
                    rook::adapters::security::SecurityManager& security,
                    rook::ports::StorePort& store)
{
    auto config_json = store.loadConfig();
    if (config_json.empty() || config_json == "{}")
        return;

    try {
        auto j = nlohmann::json::parse(config_json);
        if (!j.contains("mcp_servers") || !j["mcp_servers"].is_array())
            return;

        for (auto& s : j["mcp_servers"]) {
            rook::adapters::mcp::McpServerConfig cfg;
            cfg.id = s.value("id", "");
            cfg.enabled = s.value("enabled", true);
            cfg.source = s.value("source", "manual");
            if (!cfg.source.empty() && cfg.source != "manual")
                continue;

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

            if (valid)
                mgr.addServer(std::move(cfg));
        }

        security.loadFromConfig(j["mcp_servers"].dump());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to parse MCP config: {}", e.what());
    }
}

std::unique_ptr<rook::adapters::server::GrpcServiceAdapter>
makeAdapter(rook::core::DomainActor& actor,
            rook::ports::StorePort& store,
            rook::ports::ExtensionPort* extensions)
{
    return std::make_unique<rook::adapters::server::GrpcServiceAdapter>(
        actor, actor.sync(), store, extensions);
}

} // anonymous namespace

int main(int argc, char** argv)
{
    RookdConfig cfg;
    parseArgs(argc, argv, cfg);
    setupLogging(cfg.verbose);

    if (!cfg.generate_token_for.empty()) {
        if (cfg.jwt_secret.empty()) {
            fmt::print(stderr, "Error: --jwt-secret required for --generate-token\n");
            return 1;
        }
        auto payload = rook::auth::JwtPayload{
            cfg.generate_token_for,
            "rookd",
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
                + 86400 * 365,
        };
        fmt::print("{}\n", rook::auth::generateToken(payload, cfg.jwt_secret));
        return 0;
    }

    bool multi_tenant = !cfg.jwt_secret.empty();
    if (multi_tenant) {
        SPDLOG_INFO("rookd starting — multi-tenant mode, listen={}", cfg.listen_address);
    } else {
        SPDLOG_INFO("rookd starting — single-tenant mode, data={} listen={}",
            cfg.data_dir, cfg.listen_address);
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    auto store = rook::adapters::store::makeJsonStore(cfg.data_dir);
    auto secrets = std::make_unique<rook::adapters::SecretStore>(
        "io.github.fleischerdesign.Rook");

    auto llm = rook::adapters::llm::makeMultiProviderAdapter();

    auto security = std::make_unique<rook::adapters::security::SecurityManager>();

    auto mcp_manager = std::make_unique<rook::adapters::mcp::McpServerManager>();

    auto composite = std::make_unique<rook::adapters::composite::CompositeToolPort>();
    auto guarded_builtin =
        std::make_unique<rook::adapters::security::SecurityGuardedToolPort>(
            *security,
            rook::adapters::builtin::makeBuiltinToolPort(),
            "builtin");
    composite->addPort(std::move(guarded_builtin));

    auto extensions = std::make_unique<rook::adapters::extension::ExtensionManager>(
        cfg.data_dir, mcp_manager.get());

    rook::core::SettingsLoader settings;
    settings.load(*store, *llm, *secrets);

    loadMcpServers(*mcp_manager, *security, *store);

    if (mcp_manager->serverCount() > 0) {
        mcp_manager->startAll();
        mcp_manager->setSecurityPort(security.get());
        composite->addPort(
            rook::adapters::mcp::makeMcpToolPort(*mcp_manager));
    }

    auto tool_port = std::move(composite);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(cfg.listen_address,
        grpc::InsecureServerCredentials());

    if (multi_tenant) {
        auto tenant_mgr = std::make_shared<
            rook::adapters::server::TenantManager>(cfg.tenants_dir);

        auto tenant_adapter = std::make_shared<
            rook::adapters::server::TenantServiceAdapter>(
                *tenant_mgr, cfg.jwt_secret);

        builder.RegisterService(tenant_adapter.get());

        auto server = builder.BuildAndStart();
        if (!server) {
            SPDLOG_ERROR("Failed to start gRPC server on {}", cfg.listen_address);
            return 1;
        }

        SPDLOG_INFO("gRPC server listening on {} (multi-tenant)", cfg.listen_address);

        while (!g_shutdown.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(250));

        SPDLOG_INFO("Shutting down...");

        server->Shutdown();
        server->Wait();
        tenant_mgr->shutdown();
    } else {
        auto actor = std::make_unique<rook::core::DomainActor>();

        auto adapter = makeAdapter(*actor, *store, extensions.get());
        auto* adapter_ptr = adapter.get();

        actor->start(*llm, *tool_port,
            nullptr, store.get(),
            [adapter_ptr](rook::domain::DomainEvent event) {
                adapter_ptr->onDomainEvent(std::move(event));
            },
            extensions.get(), nullptr);

        builder.RegisterService(adapter.get());

        auto server = builder.BuildAndStart();
        if (!server) {
            SPDLOG_ERROR("Failed to start gRPC server on {}", cfg.listen_address);
            return 1;
        }

        SPDLOG_INFO("gRPC server listening on {} (single-tenant)", cfg.listen_address);

        while (!g_shutdown.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(250));

        SPDLOG_INFO("Shutting down...");

        adapter->shutdown();
        server->Shutdown();
        server->Wait();
        actor->stop();
    }

    if (mcp_manager)
        mcp_manager->stopAll();

    SPDLOG_INFO("rookd stopped");
    return 0;
}
