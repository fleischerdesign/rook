#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <compare>

namespace rook::sync {

struct HlcTimestamp {
    int64_t wall_time_ms = 0;
    int32_t logical_counter = 0;
    std::string node_id;

    auto operator<=>(const HlcTimestamp& other) const = default;
};

class HybridLogicalClock {
public:
    explicit HybridLogicalClock(std::string node_id);

    [[nodiscard]] HlcTimestamp now();

    void observe(const HlcTimestamp& remote);

    [[nodiscard]] const std::string& nodeId() const { return m_node_id; }

private:
    int64_t m_last_wall = 0;
    int32_t m_logical = 0;
    std::string m_node_id;
};

struct HlcOrder {
    bool operator()(const HlcTimestamp& a, const HlcTimestamp& b) const noexcept;
};

} // namespace rook::sync
