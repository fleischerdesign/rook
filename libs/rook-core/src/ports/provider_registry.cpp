#include "rook/ports/llm_port.hpp"

namespace rook::ports {

ProviderRegistry& ProviderRegistry::instance() {
    static ProviderRegistry registry;
    return registry;
}

ProviderRegistry::ProviderRegistry() {
    registerBuiltins();
}

void ProviderRegistry::registerBuiltins() {
    registerType({"ollama",    "Ollama (local)",   "http://localhost:11434",     "llama3.1",
                  {"llama3.1", "mistral", "phi4", "codellama", "gemma3"}});
    registerType({"openai",    "OpenAI",            "https://api.openai.com",    "gpt-4o",
                  {"gpt-4o", "gpt-4o-mini", "o4-mini"}});
    registerType({"deepseek",  "DeepSeek",          "https://api.deepseek.com",   "deepseek-chat",
                  {"deepseek-chat", "deepseek-reasoner"}});
    registerType({"anthropic", "Anthropic",         "https://api.anthropic.com", "claude-sonnet-4-20250514",
                  {"claude-sonnet-4-20250514", "claude-opus-4-20250514", "claude-haiku-4-20250514"}});
}

void ProviderRegistry::registerType(ProviderTypeInfo info) {
    m_types.push_back(std::move(info));
}

std::vector<ProviderTypeInfo> ProviderRegistry::all() const {
    return m_types;
}

std::optional<ProviderTypeInfo> ProviderRegistry::find(std::string_view id) const {
    for (const auto& t : m_types) {
        if (t.id == id) return t;
    }
    return std::nullopt;
}

} // namespace rook::ports
