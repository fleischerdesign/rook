#include "rook/sync/hlc.hpp"
#include <chrono>
#include <algorithm>

namespace rook::sync {

static int64_t physical_now_ms()
{
    auto now = std::chrono::system_clock::now();
    auto dur = now.time_since_epoch();
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
}

HybridLogicalClock::HybridLogicalClock(std::string node_id)
    : m_node_id(std::move(node_id))
{
}

HlcTimestamp HybridLogicalClock::now()
{
    int64_t pt = physical_now_ms();
    int64_t l = std::max(m_last_wall, pt);

    if (l == m_last_wall) {
        m_logical += 1;
    } else {
        m_logical = 0;
    }

    m_last_wall = l;

    return HlcTimestamp{l, m_logical, m_node_id};
}

void HybridLogicalClock::observe(const HlcTimestamp& remote)
{
    int64_t pt = physical_now_ms();

    int64_t l = std::max({m_last_wall,
                          remote.wall_time_ms,
                          pt});

    if (l == m_last_wall && l == remote.wall_time_ms) {
        m_logical = std::max(m_logical, remote.logical_counter) + 1;
    } else if (l == m_last_wall) {
        m_logical += 1;
    } else if (l == remote.wall_time_ms) {
        m_logical = remote.logical_counter + 1;
    } else {
        m_logical = 0;
    }

    m_last_wall = l;
}

bool HlcOrder::operator()(const HlcTimestamp& a,
                           const HlcTimestamp& b) const noexcept
{
    if (a.wall_time_ms != b.wall_time_ms)
        return a.wall_time_ms < b.wall_time_ms;
    if (a.logical_counter != b.logical_counter)
        return a.logical_counter < b.logical_counter;
    return a.node_id < b.node_id;
}

} // namespace rook::sync
