#include "rook/adapters/llm/openai_compatible_adapter.hpp"
#include "rook/adapters/llm/llm_http_client.hpp"

namespace rook::adapters::llm {

class OpenAiAdapter final : public OpenAiCompatibleAdapter {
public:
    explicit OpenAiAdapter(std::unique_ptr<LlmHttpClient> http)
        : OpenAiCompatibleAdapter(
            std::move(http),
            "https://api.openai.com",
            "gpt-4o"
        )
    {}
};

std::unique_ptr<ports::LlmPort> makeOpenAiAdapter() {
    return std::make_unique<OpenAiAdapter>(makeCurlHttpClient());
}

} // namespace rook::adapters::llm
