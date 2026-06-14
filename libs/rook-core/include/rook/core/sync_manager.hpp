#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <functional>

namespace rook::sync {
class SyncEngine;
}

namespace rook::core {
class PeerManager;

enum class SyncStatus {
    Offline = 0,
    Connecting,
    Connected,
    Syncing,
    Error,
};

class SyncManager {
public:
    using StatusCallback = std::function<void(SyncStatus)>;

    SyncManager(rook::sync::SyncEngine& sync,
                rook::core::PeerManager& peers);

    ~SyncManager();

    SyncManager(const SyncManager&) = delete;
    SyncManager& operator=(const SyncManager&) = delete;

    void onLocalChange();
    void onPeerConnected(std::string_view address);
    void setStatusCallback(StatusCallback cb) { m_status_callback = std::move(cb); }

private:
    void syncLoop(std::stop_token token);
    void pushToAll();
    void pushToPeer(std::string_view address);
    void pullFromPeer(std::string_view address);

    struct RetryState {
        std::chrono::steady_clock::time_point next_retry;
        std::chrono::milliseconds backoff{1000};
        std::vector<uint8_t> data;
    };

    rook::sync::SyncEngine& m_sync;
    rook::core::PeerManager& m_peers;
    StatusCallback m_status_callback;

    std::jthread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::chrono::steady_clock::time_point m_deadline;
    bool m_dirty = false;

    std::unordered_map<std::string, RetryState> m_retries;
};

} // namespace rook::core
