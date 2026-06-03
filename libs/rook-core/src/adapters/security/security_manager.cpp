#include "rook/adapters/security/security_manager.hpp"
#include "rook/ports/tool_port.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::adapters::security {

bool SecurityManager::isAllowed(std::string_view server_id,
                                const ports::ToolCall& call) const
{
    auto it = m_capabilities.find(server_id);
    if (it == m_capabilities.end()) {
        return true;
    }

    const auto& cap = it->second;

    if (call.name == "read_file" || call.name == "list_directory") {
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args.value("path", "");
            return cap.allowsRead(path);
        } catch (...) {
            return false;
        }
    }

    if (call.name == "write_file") {
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string path = args.value("path", "");
            return cap.allowsWrite(path);
        } catch (...) {
            return false;
        }
    }

    return true;
}

void SecurityManager::loadFromConfig(std::string_view config_json)
{
    m_capabilities.clear();

    if (config_json.empty()) return;

    try {
        auto config = nlohmann::json::parse(config_json);
        if (!config.is_array()) return;

        for (const auto& entry : config) {
            std::string id = entry.value("id", "");
            if (id.empty()) continue;

            auto caps = entry.value("capabilities",
                                     nlohmann::json::object());

            auto builder = Capability::grant();

            if (caps.contains("read")) {
                for (const auto& p : caps["read"]) {
                    if (p.is_string())
                        builder.read(p.get<std::string>());
                }
            }

            if (caps.contains("write")) {
                for (const auto& p : caps["write"]) {
                    if (p.is_string())
                        builder.write(p.get<std::string>());
                }
            }

            if (caps.contains("network")) {
                if (caps["network"].is_boolean()) {
                    if (caps["network"].get<bool>())
                        builder.allowNetwork();
                    else
                        builder.noNetwork();
                }
            }

            if (caps.contains("max_memory_mb")) {
                if (caps["max_memory_mb"].is_number_integer()) {
                    builder.maxMemoryMb(caps["max_memory_mb"].get<int64_t>());
                }
            }

            if (caps.contains("max_cpu_time_secs")) {
                if (caps["max_cpu_time_secs"].is_number_integer()) {
                    builder.maxCpuTime(
                        std::chrono::seconds(
                            caps["max_cpu_time_secs"].get<int64_t>()));
                }
            }

            m_capabilities.emplace(std::move(id), builder.build());
        }

        spdlog::info("SecurityManager: loaded {} capability grants",
                     m_capabilities.size());
    } catch (const std::exception& e) {
        spdlog::error("SecurityManager: failed to parse config: {}", e.what());
    }
}

const Capability* SecurityManager::findCapability(
    std::string_view server_id) const
{
    auto it = m_capabilities.find(server_id);
    if (it != m_capabilities.end()) return &it->second;
    return nullptr;
}

void SecurityManager::setCapability(std::string server_id, Capability cap)
{
    m_capabilities[std::move(server_id)] = std::move(cap);
}

} // namespace rook::adapters::security
