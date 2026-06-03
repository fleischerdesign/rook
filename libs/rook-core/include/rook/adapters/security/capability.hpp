#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <chrono>

namespace rook::adapters::security {

class Capability {
public:
    class Builder;

    static Builder grant();

    bool allowsRead(std::string_view path) const;
    bool allowsWrite(std::string_view path) const;
    bool allowsNetwork() const { return m_allow_network; }

    int64_t maxMemoryMb() const { return m_max_memory_mb; }
    int64_t maxCpuTimeSecs() const { return m_max_cpu_time_secs; }

    const std::vector<std::string>& readPaths() const { return m_read_paths; }
    const std::vector<std::string>& writePaths() const { return m_write_paths; }


    Capability() = default;

private:
    static bool matchesPath(std::string_view prefix,
                            std::string_view path);

    std::vector<std::string> m_read_paths;
    std::vector<std::string> m_write_paths;
    bool m_allow_network = false;
    int64_t m_max_memory_mb = 256;
    int64_t m_max_cpu_time_secs = 60;
};

class Capability::Builder {
public:
    Builder() = default;

    Builder& read(std::string path);
    Builder& write(std::string path);
    Builder& noNetwork();
    Builder& allowNetwork();
    Builder& maxMemoryMb(int64_t mb);
    Builder& maxCpuTime(std::chrono::seconds t);

    Capability build();

private:
    Capability m_cap;
};

} // namespace rook::adapters::security

inline rook::adapters::security::Capability::Builder
rook::adapters::security::Capability::grant()
{
    return Builder{};
}
