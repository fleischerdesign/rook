#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <cstdint>

namespace rook::adapters::llm {

struct HttpResponse {
    int32_t status_code = 0;
    std::string body;
};

class LlmHttpClient {
public:
    virtual ~LlmHttpClient() = default;

    virtual HttpResponse post(
        std::string_view url,
        std::string_view body,
        const std::vector<std::pair<std::string, std::string>>& headers
    ) = 0;

    virtual void postStream(
        std::string_view url,
        std::string_view body,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::function<void(std::string_view line)> on_line,
        std::function<void(int32_t status_code)> on_error
    ) = 0;

    virtual void cancel() {}
};

std::unique_ptr<LlmHttpClient> makeCurlHttpClient();

} // namespace rook::adapters::llm
