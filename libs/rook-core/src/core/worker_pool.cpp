#include "rook/core/worker_pool.hpp"
#include "rook/core/lockfree_queue.hpp"
#include <spdlog/spdlog.h>

namespace rook::core {

WorkerPool::WorkerPool(std::size_t num_threads)
    : m_queue(std::make_unique<MpMcQueue<std::function<void()>>>(256))
{
    for (std::size_t i = 0; i < num_threads; ++i) {
        m_threads.emplace_back([this](std::stop_token token) {
            while (!token.stop_requested()) {
                auto task = m_queue->pop();
                if (task) {
                    try {
                        (*task)();
                    } catch (const std::exception& e) {
                        spdlog::error("WorkerPool: task threw: {}", e.what());
                    } catch (...) {
                        spdlog::error("WorkerPool: task threw unknown exception");
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            while (auto task = m_queue->pop()) {
                try {
                    (*task)();
                } catch (const std::exception& e) {
                    spdlog::error("WorkerPool: drain task threw: {}", e.what());
                } catch (...) {}
            }
        });
    }
    spdlog::info("WorkerPool: started {} threads", num_threads);
}

WorkerPool::~WorkerPool() {
    stop();
}

void WorkerPool::submit(std::function<void()> task) {
    if (!m_queue->push(std::move(task))) {
        spdlog::warn("WorkerPool: queue full, task dropped");
    }
}

void WorkerPool::stop() {
    if (m_stop.stop_requested()) return;
    m_stop.request_stop();
    for (auto& t : m_threads)
        if (t.joinable()) t.request_stop();
    for (auto& t : m_threads)
        if (t.joinable()) t.join();
    spdlog::info("WorkerPool: stopped");
}

} // namespace rook::core
