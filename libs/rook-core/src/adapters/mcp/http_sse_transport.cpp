#include "rook/adapters/mcp/http_sse_transport.hpp"
#include "rook/adapters/llm/openai_compatible_adapter.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdlib>

namespace rook::adapters::mcp {

HttpSseTransport::HttpSseTransport(
    std::string url,
    std::vector<std::pair<std::string, std::string>> headers)
    : m_url(std::move(url))
    , m_headers(std::move(headers))
    , m_http(llm::makeCurlHttpClient())
{
}

HttpSseTransport::~HttpSseTransport()
{
    stop();
}

void HttpSseTransport::setMessageHandler(MessageHandler handler)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handler = std::move(handler);
}

void HttpSseTransport::start()
{
    if (m_running.exchange(true)) return;

    if (m_listener_thread.joinable()) m_listener_thread.join();

    HttpSseTransport* self = this;
    m_listener_thread = std::thread([self]() {
        auto hdrs = self->m_headers;
        hdrs.push_back({"Accept", "text/event-stream"});

        for (auto& [key, val] : hdrs) {
            val = resolveEnvVars(val);
        }

        self->m_http->getStream(self->m_url, hdrs,
            [self](std::string_view line) {
                llm::SseParser parser;
                parser.feed(std::string(line) + "\n",
                    [self](const llm::SseEvent& event) {
                        if (!event.data.empty()) {
                            MessageHandler handler;
                            {
                                std::lock_guard<std::mutex> lock(self->m_mutex);
                                handler = self->m_handler;
                            }
                            if (handler) handler(event.data);
                        }
                    });
            },
            [self](int32_t code) {
                if (self->m_running.load()) {
                    spdlog::warn("HttpSseTransport: listener HTTP {} for {}",
                                 code, self->m_url);
                }
            });
    });

    spdlog::info("HttpSseTransport: started for {}", m_url);
}

void HttpSseTransport::stop()
{
    if (!m_running.exchange(false)) return;
    m_http->cancel();

    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }

    spdlog::info("HttpSseTransport: stopped for {}", m_url);
}

void HttpSseTransport::send(std::string_view json)
{
    if (!m_running.load()) {
        spdlog::warn("HttpSseTransport: send() called while not running");
        return;
    }

    auto all_headers = m_headers;
    all_headers.push_back({"Content-Type", "application/json"});
    all_headers.push_back({"Accept", "text/event-stream"});

    for (auto& [key, val] : all_headers) {
        val = resolveEnvVars(val);
    }

    spdlog::debug("HttpSseTransport: POST {} bytes to {}", json.size(), m_url);

    auto response = m_http->post(m_url, json, all_headers);

    if (response.body.empty()) {
        spdlog::warn("HttpSseTransport: empty response body (HTTP {})",
                     response.status_code);
        return;
    }

    bool handled = false;

    llm::SseParser parser;
    parser.feed(response.body, [&](const llm::SseEvent& event) {
        if (!event.data.empty()) {
            MessageHandler handler;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                handler = m_handler;
            }
            if (handler) {
                handler(event.data);
                handled = true;
            }
        }
    });

    if (!handled) {
        MessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            handler = m_handler;
        }
        if (handler) {
            handler(response.body);
        }
    }
}

std::string HttpSseTransport::resolveEnvVars(std::string_view value)
{
    std::string result;
    result.reserve(value.size());

    size_t i = 0;
    while (i < value.size()) {
        if (value[i] == '$' && i + 1 < value.size() && value[i + 1] == '{') {
            size_t end = value.find('}', i + 2);
            if (end != std::string_view::npos) {
                auto var_name = value.substr(i + 2, end - i - 2);
                const char* env_val = std::getenv(std::string(var_name).c_str());
                result += env_val ? env_val : "";
                i = end + 1;
                continue;
            }
        }
        result += value[i];
        ++i;
    }

    return result;
}

std::unique_ptr<McpTransport> makeHttpSseTransport(
    std::string url,
    std::vector<std::pair<std::string, std::string>> headers)
{
    return std::make_unique<HttpSseTransport>(
        std::move(url), std::move(headers));
}

} // namespace rook::adapters::mcp
