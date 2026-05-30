#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
#include "rook/adapters/llm/threaded_adapter.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <random>
#include <sstream>

namespace rook::adapters::llm {

namespace {

std::string generateProviderId() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << "prov-" << std::hex << dis(gen);
    return oss.str();
}

} // namespace

MultiProviderLlmAdapter::MultiProviderLlmAdapter() {
}

void MultiProviderLlmAdapter::configure(const ports::LlmConfig& /*config*/) {
}

void MultiProviderLlmAdapter::streamChat(
    std::string_view chat_id,
    const std::vector<ports::LlmMessage>& messages,
    std::function<void(std::string_view chunk, bool is_final)> on_chunk,
    std::string_view model
) {
    std::string real_model;
    std::string provider_id;

    if (!model.empty()) {
        auto colon = model.find(':');
        if (colon != std::string::npos) {
            provider_id = model.substr(0, colon);
            real_model = model.substr(colon + 1);
        } else {
            real_model = model;
        }
    }

    auto it = m_provider_adapters.find(provider_id);
    if (it == m_provider_adapters.end() && !m_provider_adapters.empty()) {
        it = m_provider_adapters.begin();
    }

    if (it != m_provider_adapters.end()) {
        it->second->streamChat(chat_id, messages, std::move(on_chunk), real_model);
    } else {
        spdlog::error("No adapters available -- streamChat aborted");
    }
}

std::vector<ports::LlmProviderConfig> MultiProviderLlmAdapter::listProviders() const {
    return m_providers;
}

void MultiProviderLlmAdapter::addProvider(const ports::LlmProviderConfig& provider) {
    auto prov = provider;
    prov.id = generateProviderId();
    m_providers.push_back(prov);

    auto concrete = createConcreteAdapter(prov);
    concrete->configure(ports::LlmConfig{
        .provider = prov.type,
        .model = prov.default_model,
        .api_key = prov.api_key,
        .base_url = prov.base_url,
        .max_tokens = 4096,
        .temperature = 0.7f,
        .system_prompt = "You are Rook, a helpful AI assistant.",
    });
    m_provider_adapters[prov.id] = std::move(concrete);
}

void MultiProviderLlmAdapter::updateProvider(const ports::LlmProviderConfig& provider) {
    auto it = std::ranges::find_if(m_providers,
        [&](const auto& p) { return p.id == provider.id; });

    if (it != m_providers.end()) {
        *it = provider;
        auto concrete = createConcreteAdapter(provider);
        concrete->configure(ports::LlmConfig{
            .provider = provider.type,
            .model = provider.default_model,
            .api_key = provider.api_key,
            .base_url = provider.base_url,
            .max_tokens = 4096,
            .temperature = 0.7f,
            .system_prompt = "You are Rook, a helpful AI assistant.",
        });
        m_provider_adapters[provider.id] = std::move(concrete);
    }
}

void MultiProviderLlmAdapter::removeProvider(std::string_view id) {
    std::erase_if(m_providers, [id](const auto& p) { return p.id == id; });
    m_provider_adapters.erase(std::string(id));
}

std::optional<ports::LlmProviderConfig> MultiProviderLlmAdapter::activeProvider() const {
    if (!m_providers.empty()) return m_providers.front();
    return std::nullopt;
}

std::unique_ptr<ports::LlmPort> MultiProviderLlmAdapter::createConcreteAdapter(
    const ports::LlmProviderConfig& provider
) {
    std::unique_ptr<ports::LlmPort> adapter;

    if (provider.type == "openai") {
        adapter = makeOpenAiAdapter();
    } else if (provider.type == "deepseek") {
        adapter = makeDeepSeekAdapter();
    } else if (provider.type == "anthropic") {
        adapter = makeAnthropicAdapter();
    } else {
        adapter = makeOllamaAdapter();
    }

    return std::make_unique<ThreadedLlmAdapter>(std::move(adapter));
}

std::unique_ptr<MultiProviderLlmAdapter> makeMultiProviderAdapter() {
    return std::make_unique<MultiProviderLlmAdapter>();
}

} // namespace rook::adapters::llm
