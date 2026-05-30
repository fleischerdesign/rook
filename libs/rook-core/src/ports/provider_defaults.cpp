#include "rook/ports/llm_port.hpp"

namespace rook::ports {

std::string ProviderDefaults::displayName(std::string_view type) {
    if (type == "ollama")    return "Ollama (local)";
    if (type == "openai")    return "OpenAI";
    if (type == "deepseek")  return "DeepSeek";
    if (type == "anthropic") return "Anthropic";
    return "Unknown";
}

std::string ProviderDefaults::baseUrl(std::string_view type) {
    if (type == "ollama")    return "http://localhost:11434";
    if (type == "openai")    return "https://api.openai.com";
    if (type == "deepseek")  return "https://api.deepseek.com";
    if (type == "anthropic") return "https://api.anthropic.com";
    return "";
}

std::string ProviderDefaults::defaultModel(std::string_view type) {
    if (type == "ollama")    return "llama3.1";
    if (type == "openai")    return "gpt-4o";
    if (type == "deepseek")  return "deepseek-chat";
    if (type == "anthropic") return "claude-sonnet-4-20250514";
    return "";
}

} // namespace rook::ports
