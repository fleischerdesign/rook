#include "rook/ports/model_discovery_port.hpp"
#include "rook/adapters/llm/llm_http_client.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using namespace rook::adapters::llm;

namespace rook::adapters::model {

class OllamaModelDiscovery final : public ports::ModelDiscoveryPort {
public:
    explicit OllamaModelDiscovery(std::string base_url)
        : m_base_url(std::move(base_url)), m_http(makeCurlHttpClient()) {}

    std::vector<ports::ModelInfo> fetchModels(std::string_view /*api_key*/) override {
        auto response = m_http->post(
            m_base_url + "/api/tags", "", {{"Content-Type", "application/json"}});

        std::vector<ports::ModelInfo> result;
        try {
            auto j = nlohmann::json::parse(response.body);
            if (j.contains("models")) {
                for (const auto& m : j["models"]) {
                    ports::ModelInfo info;
                    info.id = m["name"].get<std::string>();
                    info.display_name = info.id;
                    result.push_back(info);
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Ollama model parse: {}", e.what());
        }

        spdlog::info("Ollama: fetched {} models", result.size());
        return result;
    }

private:
    std::string m_base_url;
    std::unique_ptr<LlmHttpClient> m_http;
};

class OpenAiModelDiscovery final : public ports::ModelDiscoveryPort {
public:
    explicit OpenAiModelDiscovery(std::string base_url)
        : m_base_url(std::move(base_url)), m_http(makeCurlHttpClient()) {}

    std::vector<ports::ModelInfo> fetchModels(std::string_view api_key) override {
        auto response = m_http->post(
            m_base_url + "/models", "",
            {{"Authorization", "Bearer " + std::string(api_key)},
             {"Content-Type", "application/json"}});

        std::vector<ports::ModelInfo> result;
        try {
            auto j = nlohmann::json::parse(response.body);
            if (j.contains("data")) {
                for (const auto& m : j["data"]) {
                    ports::ModelInfo info;
                    info.id = m["id"].get<std::string>();
                    info.display_name = info.id;
                    result.push_back(info);
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("OpenAI model parse: {}", e.what());
        }

        spdlog::info("OpenAI-compat: fetched {} models", result.size());
        return result;
    }

private:
    std::string m_base_url;
    std::unique_ptr<LlmHttpClient> m_http;
};

class AnthropicModelDiscovery final : public ports::ModelDiscoveryPort {
public:
    AnthropicModelDiscovery() : m_http(makeCurlHttpClient()) {}

    std::vector<ports::ModelInfo> fetchModels(std::string_view api_key) override {
        auto response = m_http->post(
            "https://api.anthropic.com/v1/models", "",
            {{"x-api-key", std::string(api_key)},
             {"anthropic-version", "2023-06-01"},
             {"Content-Type", "application/json"}});

        std::vector<ports::ModelInfo> result;
        try {
            auto j = nlohmann::json::parse(response.body);
            if (j.contains("data")) {
                for (const auto& m : j["data"]) {
                    ports::ModelInfo info;
                    info.id = m["id"].get<std::string>();
                    info.display_name = m.value("display_name", info.id);
                    result.push_back(info);
                }
            }
        } catch (const std::exception& e) {
            spdlog::error("Anthropic model parse: {}", e.what());
        }

        spdlog::info("Anthropic: fetched {} models", result.size());
        return result;
    }

private:
    std::unique_ptr<LlmHttpClient> m_http;
};

std::unique_ptr<ports::ModelDiscoveryPort> makeOllamaDiscovery(std::string_view base_url) {
    return std::make_unique<OllamaModelDiscovery>(std::string(base_url));
}

std::unique_ptr<ports::ModelDiscoveryPort> makeOpenAiDiscovery(std::string_view base_url) {
    return std::make_unique<OpenAiModelDiscovery>(std::string(base_url));
}

std::unique_ptr<ports::ModelDiscoveryPort> makeAnthropicDiscovery() {
    return std::make_unique<AnthropicModelDiscovery>();
}

} // namespace rook::adapters::model
