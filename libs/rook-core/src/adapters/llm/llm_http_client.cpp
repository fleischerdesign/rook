#include "rook/adapters/llm/llm_http_client.hpp"
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <atomic>

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

int progressCallback(void* userp, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                     curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* flag = static_cast<std::atomic<bool>*>(userp);
    return flag->load() ? 1 : 0;
}

class CurlHttpClient final : public LlmHttpClient {
public:
    void cancel() override { m_abort.store(true); }

    HttpResponse get(
        std::string_view url,
        const std::vector<std::pair<std::string, std::string>>& headers
    ) override {
        auto* handle = curl_easy_init();
        if (!handle) {
            spdlog::error("curl_easy_init failed for get");
            return {0, {}};
        }

        std::string response_body;
        curl_easy_setopt(handle, CURLOPT_URL, url.data());
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_body);

        auto* hlist = buildHeaders(headers);
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, hlist);

        auto res = curl_easy_perform(handle);
        int32_t status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        } else {
            spdlog::error("CURL error: {} ({})", curl_easy_strerror(res), static_cast<int>(res));
        }

        curl_slist_free_all(hlist);
        curl_easy_cleanup(handle);
        return {status, std::move(response_body)};
    }

    HttpResponse post(
        std::string_view url,
        std::string_view body,
        const std::vector<std::pair<std::string, std::string>>& headers
    ) override {
        auto* handle = curl_easy_init();
        if (!handle) {
            spdlog::error("curl_easy_init failed");
            return {0, {}};
        }

        m_abort.store(false);
        std::string response_body;
        curl_easy_setopt(handle, CURLOPT_URL, url.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &m_abort);

        auto* hlist = buildHeaders(headers);
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, hlist);

        auto res = curl_easy_perform(handle);
        int32_t status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        } else {
            spdlog::error("CURL error: {} ({})", curl_easy_strerror(res), static_cast<int>(res));
        }

        curl_slist_free_all(hlist);
        curl_easy_cleanup(handle);
        return {status, std::move(response_body)};
    }

    void postStream(
        std::string_view url,
        std::string_view body,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::function<void(std::string_view)> on_line,
        std::function<void(int32_t)> on_error
    ) override {
        spdlog::info("CurlHttp: postStream url={}", url);
        auto* handle = curl_easy_init();
        if (!handle) {
            spdlog::error("curl_easy_init failed for postStream");
            return;
        }

        m_abort.store(false);
        StreamContext ctx;
        ctx.on_line = std::move(on_line);

        curl_easy_setopt(handle, CURLOPT_URL, url.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, streamCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &m_abort);

        auto* hlist = buildHeaders(headers);
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, hlist);

        spdlog::info("CurlHttp: performing postStream...");
        auto res = curl_easy_perform(handle);
        int32_t status = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        spdlog::info("CurlHttp: postStream done res={} http={}", static_cast<int>(res), status);

        if (res != CURLE_OK) {
            spdlog::error("CURL error: {} (HTTP {})", curl_easy_strerror(res), status);
            on_error(status);
        } else if (status >= 400) {
            spdlog::error("CurlHttp: HTTP error {}", status);
            on_error(status);
        }

        curl_slist_free_all(hlist);
        curl_easy_cleanup(handle);
    }

    void getStream(
        std::string_view url,
        const std::vector<std::pair<std::string, std::string>>& headers,
        std::function<void(std::string_view)> on_line,
        std::function<void(int32_t)> on_error
    ) override {
        auto* handle = curl_easy_init();
        if (!handle) {
            spdlog::error("curl_easy_init failed for getStream");
            return;
        }

        m_abort.store(false);
        StreamContext ctx;
        ctx.on_line = std::move(on_line);

        curl_easy_setopt(handle, CURLOPT_URL, url.data());
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, streamCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &m_abort);

        auto* hlist = buildHeaders(headers);
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, hlist);

        auto res = curl_easy_perform(handle);
        int32_t status = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        spdlog::info("CurlHttp: postStream done res={} http={}", static_cast<int>(res), status);

        if (res != CURLE_OK) {
            spdlog::error("CURL error: {} (HTTP {})", curl_easy_strerror(res), status);
            on_error(status);
        } else if (status >= 400) {
            spdlog::error("CurlHttp: HTTP error {}", status);
            on_error(status);
        }

        curl_slist_free_all(hlist);
        curl_easy_cleanup(handle);
    }

private:
    std::atomic<bool> m_abort{false};
};

} // namespace

std::unique_ptr<LlmHttpClient> makeCurlHttpClient() {
    return std::make_unique<CurlHttpClient>();
}

} // namespace rook::adapters::llm
