#include "rook/adapters/llm/threaded_adapter.hpp"
#include <spdlog/spdlog.h>

namespace rook::adapters::llm {

ThreadedLlmAdapter::ThreadedLlmAdapter(std::unique_ptr<ports::LlmPort> inner)
    : m_inner(std::move(inner))
{}

ThreadedLlmAdapter::~ThreadedLlmAdapter() {
    if (m_thread.joinable()) {
        m_stop_source.request_stop();
        m_thread.join();
    }
}

void ThreadedLlmAdapter::configure(const ports::LlmConfig& config) {
    m_inner->configure(config);
}

void ThreadedLlmAdapter::streamChat(
    std::string_view chat_id,
    const std::vector<ports::LlmMessage>& messages,
    std::function<void(std::string_view chunk, bool is_final)> on_chunk,
    std::string_view model
) {
    auto self = std::this_thread::get_id();

    if (m_thread.joinable()) {
        if (m_thread.get_id() == self) {
            m_stop_source.request_stop();
            m_inner->streamChat(chat_id, messages, std::move(on_chunk), model);
            return;
        }
        m_stop_source.request_stop();
        m_thread.join();
    }

    m_stop_source = std::stop_source();

    auto chat = std::string(chat_id);
    auto msgs = messages;
    auto model_str = std::string(model);
    std::function<void(std::string_view, bool)> callback = std::move(on_chunk);

    m_thread = std::jthread(
        [this, chat, msgs = std::move(msgs), callback = std::move(callback), model_str](
            std::stop_token token
        ) mutable {
            (void)token;
            m_inner->streamChat(chat, msgs, std::move(callback), model_str);
        });
}

std::vector<ports::LlmProviderConfig> ThreadedLlmAdapter::listProviders() const {
    return m_inner->listProviders();
}

void ThreadedLlmAdapter::addProvider(const ports::LlmProviderConfig& p) {
    m_inner->addProvider(p);
}

void ThreadedLlmAdapter::updateProvider(const ports::LlmProviderConfig& p) {
    m_inner->updateProvider(p);
}

void ThreadedLlmAdapter::removeProvider(std::string_view id) {
    m_inner->removeProvider(id);
}

std::optional<ports::LlmProviderConfig> ThreadedLlmAdapter::activeProvider() const {
    return m_inner->activeProvider();
}

void ThreadedLlmAdapter::cancel() {
    m_stop_source.request_stop();
    if (m_inner) {
        m_inner->cancel();
    }
}

} // namespace rook::adapters::llm
