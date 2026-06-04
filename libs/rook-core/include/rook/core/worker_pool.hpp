#pragma once

#include <memory>
#include <functional>
#include <thread>
#include <stop_token>
#include <vector>
#include <cstddef>

namespace rook::core {

template <typename T> class MpMcQueue;

class WorkerPool {
public:
    explicit WorkerPool(std::size_t num_threads = 4);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void submit(std::function<void()> task);
    void stop();

private:
    std::vector<std::jthread> m_threads;
    std::unique_ptr<MpMcQueue<std::function<void()>>> m_queue;
    std::stop_source m_stop;
};

} // namespace rook::core
