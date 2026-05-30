#include "rook/adapters/llm/llm_http_client.hpp"
#include <curl/curl.h>
#include <stdexcept>
#include <cstring>

namespace rook::adapters::llm {

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    str->append(static_cast<char*>(contents), total);
    return total;
}

struct StreamContext {
    std::string line_buffer;
    std::function<void(std::string_view)> on_line;
};

size_t streamCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* ctx = static_cast<StreamContext*>(userp);
    size_t total = size * nmemb;
    auto* data = static_cast<char*>(contents);

    for (size_t i = 0; i < total; ++i) {
        char c = data[i];
        if (c == '\n') {
            if (!ctx->line_buffer.empty() && ctx->line_buffer.back() == '\r') {
                ctx->line_buffer.pop_back();
            }
            ctx->on_line(ctx->line_buffer);
            ctx->line_buffer.clear();
        } else {
            ctx->line_buffer += c;
        }
    }
    return total;
}

class CurlHttpClient final : public LlmHttpClient {
public:
    CurlHttpClient() {
        m_handle = curl_easy_init();
        if (!m_handle) throw std::runtime_error("curl_easy_init failed");
    }

    ~CurlHttpClient() override {
        if (m_handle) curl_easy_cleanup(m_handle);
        curl_slist_free_all(m_global_headers);
    }

    HttpResponse post(
        std::string_view url,
        std::string_view body,
        const std::vector<std::pair<std::string, std::string>>& headers
    ) override {
        curl_easy_reset(m_handle);
        std::string response_body;
        curl_easy_setopt(m_handle, CURLOPT_URL, url.data());
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(m_handle, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(m_handle, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, nullptr);

        auto* hlist = buildHeaders(headers);
        curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, hlist);

        auto res = curl_easy_perform(m_handle);
        int32_t status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(m_handle, CURLINFO_RESPONSE_CODE, &status);
        }

        curl_slist_free_all(hlist);
        return {status, std::move(response_body)};
    }

    void postStream(
        std::string_view url,
        std::string_view body,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::function<void(std::string_view)> on_line,
        std::function<void(int32_t)> on_error
    ) override {
        curl_easy_reset(m_handle);
        StreamContext ctx;
        ctx.on_line = std::move(on_line);

        curl_easy_setopt(m_handle, CURLOPT_URL, url.data());
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(m_handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(m_handle, CURLOPT_WRITEFUNCTION, streamCallback);
        curl_easy_setopt(m_handle, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, nullptr);

        auto* hlist = buildHeaders(headers);
        curl_easy_setopt(m_handle, CURLOPT_HTTPHEADER, hlist);

        auto res = curl_easy_perform(m_handle);
        if (res != CURLE_OK) {
            int32_t status = 0;
            curl_easy_getinfo(m_handle, CURLINFO_RESPONSE_CODE, &status);
            on_error(status);
        }

        curl_slist_free_all(hlist);
    }

private:
    CURL* m_handle = nullptr;
    curl_slist* m_global_headers = nullptr;

    curl_slist* buildHeaders(
        const std::vector<std::pair<std::string, std::string>>& headers
    ) {
        curl_slist* hlist = nullptr;
        for (const auto& [key, value] : headers) {
            std::string header = key + ": " + value;
            hlist = curl_slist_append(hlist, header.c_str());
        }
        return hlist;
    }
};

} // namespace

std::unique_ptr<LlmHttpClient> makeCurlHttpClient() {
    return std::make_unique<CurlHttpClient>();
}

} // namespace rook::adapters::llm
