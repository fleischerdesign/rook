#include "rook/core/sync_manager.hpp"
#include "rook/core/peer_manager.hpp"
#include "rook/sync/sync_engine.hpp"
#include <spdlog/spdlog.h>

namespace rook::core {

static constexpr auto kDebounceWindow = std::chrono::milliseconds(500);
static constexpr auto kHeartbeatInterval = std::chrono::seconds(30);
static constexpr auto kMaxBackoff = std::chrono::seconds(60);

SyncManager::SyncManager(rook::sync::SyncEngine& sync,
                         rook::core::PeerManager& peers)
    : m_sync(sync)
    , m_peers(peers)
    , m_thread([this](std::stop_token token) { syncLoop(token); })
{
}

SyncManager::~SyncManager()
{
    m_thread.request_stop();
    m_cv.notify_one();
}

void SyncManager::onLocalChange()
{
    {
        std::lock_guard lock(m_mutex);
        auto now = std::chrono::steady_clock::now();
        if (!m_dirty) {
            m_deadline = now + kDebounceWindow;
            m_dirty = true;
        } else {
            m_deadline = std::min(m_deadline, now + kDebounceWindow);
        }
    }
    m_cv.notify_one();
}

void SyncManager::onPeerConnected(std::string_view address)
{
    {
        std::lock_guard lock(m_mutex);
        m_retries.erase(std::string(address));
    }

    pullFromPeer(address);
    pushToPeer(address);
}

void SyncManager::pushToAll()
{
    if (m_status_callback)
        m_status_callback(SyncStatus::Syncing);

    auto peers = m_peers.peers();
    for (const auto* peer : peers) {
        if (!peer->connected) continue;
        pushToPeer(peer->config.address);
    }

    if (m_status_callback)
        m_status_callback(SyncStatus::Connected);
}

void SyncManager::pushToPeer(std::string_view address)
{
    auto data = m_sync.serializeFullState();

    if (m_peers.pushToPeer(address, data))
        return;

    std::lock_guard lock(m_mutex);
    auto& st = m_retries[std::string(address)];
    st.data = std::move(data);
    st.next_retry = std::chrono::steady_clock::now() + st.backoff;
    auto doubled = st.backoff * 2;
    st.backoff = doubled > kMaxBackoff
        ? std::chrono::duration_cast<std::chrono::milliseconds>(kMaxBackoff)
        : doubled;

    if (m_status_callback)
        m_status_callback(SyncStatus::Error);

    SPDLOG_WARN("SyncManager: push to {} failed, retry in {}ms",
        address, st.backoff.count());
}

void SyncManager::pullFromPeer(std::string_view address)
{
    auto data = m_peers.pullFromPeer(address);
    if (data.empty())
        return;

    m_sync.mergeFullState(data);

    SPDLOG_DEBUG("SyncManager: pulled state from {} ({} B)",
        address, data.size());
}

void SyncManager::syncLoop(std::stop_token token)
{
    SPDLOG_INFO("SyncManager: sync loop started");

    while (!token.stop_requested()) {
        std::unique_lock lock(m_mutex);

        auto now = std::chrono::steady_clock::now();
        auto wait_until = now + kHeartbeatInterval;

        if (m_dirty) {
            wait_until = m_deadline;
        }

        for (auto& [addr, st] : m_retries) {
            if (st.next_retry < wait_until)
                wait_until = st.next_retry;
        }

        m_cv.wait_until(lock, wait_until);

        if (token.stop_requested())
            break;

        now = std::chrono::steady_clock::now();

        if (m_dirty && now >= m_deadline) {
            m_dirty = false;
            lock.unlock();
            pushToAll();
            lock.lock();
        }

        auto retry_now = std::vector<std::string>{};
        for (auto& [addr, st] : m_retries) {
            if (now >= st.next_retry)
                retry_now.push_back(addr);
        }

        lock.unlock();

        for (auto& addr : retry_now) {
            std::vector<uint8_t> data;

            {
                std::lock_guard rl(m_mutex);
                auto it = m_retries.find(addr);
                if (it == m_retries.end()) continue;
                if (m_peers.pushToPeer(addr, it->second.data)) {
                    m_retries.erase(it);
                    SPDLOG_INFO("SyncManager: retry push to {} succeeded", addr);
                } else {
                    it->second.next_retry = std::chrono::steady_clock::now()
                        + it->second.backoff;
                    auto doubled = it->second.backoff * 2;
                    it->second.backoff = doubled > kMaxBackoff
                        ? std::chrono::duration_cast<
                            std::chrono::milliseconds>(kMaxBackoff)
                        : doubled;
                }
            }
        }
    }

    SPDLOG_INFO("SyncManager: sync loop stopped");
}

} // namespace rook::core
