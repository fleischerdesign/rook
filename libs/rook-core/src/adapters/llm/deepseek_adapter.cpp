#include "rook/adapters/llm/openai_compatible_adapter.hpp"
#include "rook/adapters/llm/llm_http_client.hpp"

namespace rook::adapters::llm {

class DeepSeekAdapter final : public OpenAiCompatibleAdapter {
public:
    explicit DeepSeekAdapter(std::unique_ptr<LlmHttpClient> http)
        : OpenAiCompatibleAdapter(
            std::move(http),
            "https://api.deepseek.com",
            "deepseek-chat"
        )
    {}
};

std::unique_ptr<ports::LlmPort> makeDeepSeekAdapter() {
    return std::make_unique<DeepSeekAdapter>(makeCurlHttpClient());
}

} // namespace rook::adapters::llm
