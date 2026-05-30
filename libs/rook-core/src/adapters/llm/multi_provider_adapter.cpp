#include "rook/adapters/llm/multi_provider_adapter.hpp"
#include "rook/adapters/llm/llm_factory.hpp"
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
    ports::LlmProviderConfig default_ollama;
    default_ollama.id = generateProviderId();
    default_ollama.display_name = "Ollama (local)";
    default_ollama.type = "ollama";
    default_ollama.base_url = "http://localhost:11434";
    default_ollama.default_model = "llama3.1";
    default_ollama.enabled = true;
    default_ollama.is_default = true;

    m_providers.push_back(default_ollama);
    m_active_provider_id = default_ollama.id;
    m_active_adapter = createConcreteAdapter(default_ollama);
}

void MultiProviderLlmAdapter::configure(const ports::LlmConfig& config) {
    if (m_active_adapter) {
        m_active_adapter->configure(config);
    }
}

void MultiProviderLlmAdapter::streamChat(
    std::string_view chat_id,
    const std::vector<ports::LlmMessage>& messages,
    std::function<void(std::string_view chunk, bool is_final)> on_chunk
) {
    if (m_active_adapter) {
        m_active_adapter->streamChat(chat_id, messages, std::move(on_chunk));
    }
}

std::vector<ports::LlmProviderConfig> MultiProviderLlmAdapter::listProviders() const {
    return m_providers;
}

void MultiProviderLlmAdapter::addProvider(const ports::LlmProviderConfig& provider) {
    auto prov = provider;
    prov.id = generateProviderId();
    m_providers.push_back(prov);

    if (prov.is_default) {
        setDefaultProvider(prov.id);
    }
}

void MultiProviderLlmAdapter::updateProvider(const ports::LlmProviderConfig& provider) {
    auto it = std::ranges::find_if(m_providers,
        [&](const auto& p) { return p.id == provider.id; });

    if (it != m_providers.end()) {
        *it = provider;

        if (m_active_provider_id == provider.id) {
            m_active_adapter = createConcreteAdapter(provider);
        }
    }
}

void MultiProviderLlmAdapter::removeProvider(std::string_view id) {
    std::erase_if(m_providers, [id](const auto& p) { return p.id == id; });

    if (m_active_provider_id == id) {
        m_active_provider_id.clear();
        m_active_adapter.reset();

        if (!m_providers.empty()) {
            auto& first = m_providers.front();
            m_active_provider_id = first.id;
            m_active_adapter = createConcreteAdapter(first);
            spdlog::info("Switched to provider: {}", first.display_name);
        }
    }
}

void MultiProviderLlmAdapter::setDefaultProvider(std::string_view id) {
    for (auto& p : m_providers) {
        p.is_default = (p.id == id);
    }

    if (m_active_provider_id != id) {
        auto it = std::ranges::find_if(m_providers,
            [id](const auto& p) { return p.id == id; });

        if (it != m_providers.end()) {
            m_active_provider_id = id;
            m_active_adapter = createConcreteAdapter(*it);
            spdlog::info("Switched to default provider: {}", it->display_name);
        }
    }
}

std::optional<ports::LlmProviderConfig> MultiProviderLlmAdapter::activeProvider() const {
    auto it = std::ranges::find_if(m_providers,
        [this](const auto& p) { return p.id == m_active_provider_id; });

    if (it != m_providers.end()) return *it;
    return std::nullopt;
}

std::unique_ptr<ports::LlmPort> MultiProviderLlmAdapter::createConcreteAdapter(
    const ports::LlmProviderConfig& provider
) {
    if (provider.type == "openai") {
        return makeOpenAiAdapter();
    } else if (provider.type == "deepseek") {
        return makeDeepSeekAdapter();
    } else if (provider.type == "anthropic") {
        return makeAnthropicAdapter();
    } else {
        return makeOllamaAdapter();
    }
}

std::unique_ptr<MultiProviderLlmAdapter> makeMultiProviderAdapter() {
    return std::make_unique<MultiProviderLlmAdapter>();
}

} // namespace rook::adapters::llm
