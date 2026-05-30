#include "rook/core/settings.hpp"
#include "rook/adapters/secret_store.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace rook::core {

bool SettingsLoader::load(ports::StorePort& store, ports::LlmPort& llm,
                          rook::adapters::SecretStore& secrets) {
    auto json_str = store.loadConfig();
    if (json_str.empty() || json_str == "{}") {
        spdlog::info("No config found, adding default Ollama provider");

        ports::LlmProviderConfig default_ollama;
        default_ollama.display_name = "Ollama (local)";
        default_ollama.type = "ollama";
        default_ollama.base_url = "http://localhost:11434";
        default_ollama.default_model = "llama3.1";
        default_ollama.enabled = true;
        default_ollama.is_default = true;

        llm.addProvider(default_ollama);
        return false;
    }

    try {
        auto j = nlohmann::json::parse(json_str);

        if (j.contains("providers") && j["providers"].is_array()) {
            for (const auto& pj : j["providers"]) {
                ports::LlmProviderConfig prov;
                prov.display_name = pj.value("display_name", "Unnamed");
                prov.type = pj.value("type", "ollama");
                prov.base_url = pj.value("base_url", "");
                prov.default_model = pj.value("default_model", "");
                prov.enabled = pj.value("enabled", true);
                prov.is_default = pj.value("is_default", false);

                auto key_label = prov.display_name + ":" + prov.type;

                auto stored_key = secrets.load(key_label);
                if (!stored_key.empty()) {
                    prov.api_key = stored_key;
                } else if (pj.contains("api_key")) {
                    prov.api_key = pj["api_key"];
                }

                llm.addProvider(prov);
                spdlog::info("Loaded provider: {} ({})", prov.display_name, prov.type);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse config: {}", e.what());
        return false;
    }

    return true;
}

void SettingsLoader::save(ports::StorePort& store, const ports::LlmPort& llm,
                          rook::adapters::SecretStore& secrets) {
    nlohmann::json j;
    j["version"] = 1;
    j["providers"] = nlohmann::json::array();

    auto providers = llm.listProviders();
    for (const auto& p : providers) {
        nlohmann::json pj;
        pj["display_name"] = p.display_name;
        pj["type"] = p.type;
        pj["base_url"] = p.base_url;
        pj["default_model"] = p.default_model;
        pj["enabled"] = p.enabled;
        pj["is_default"] = p.is_default;

        auto key_label = p.display_name + ":" + p.type;
        if (!p.api_key.empty()) {
            secrets.store(key_label, p.api_key);
        }

        j["providers"].push_back(pj);
    }

    store.saveConfig(j.dump(2));
    spdlog::info("Config saved ({} providers)", providers.size());
}

} // namespace rook::core
