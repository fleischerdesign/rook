#include "rook/adapters/security/capability.hpp"

namespace rook::adapters::security {

Capability::Builder& Capability::Builder::read(std::string path)
{
    if (!path.empty() && path.back() != '/')
        path += '/';
    m_cap.m_read_paths.push_back(std::move(path));
    return *this;
}

Capability::Builder& Capability::Builder::write(std::string path)
{
    if (!path.empty() && path.back() != '/')
        path += '/';
    m_cap.m_write_paths.push_back(std::move(path));
    return *this;
}

Capability::Builder& Capability::Builder::noNetwork()
{
    m_cap.m_allow_network = false;
    return *this;
}

Capability::Builder& Capability::Builder::allowNetwork()
{
    m_cap.m_allow_network = true;
    return *this;
}

Capability::Builder& Capability::Builder::maxMemoryMb(int64_t mb)
{
    m_cap.m_max_memory_mb = mb;
    return *this;
}

Capability::Builder& Capability::Builder::maxCpuTime(std::chrono::seconds t)
{
    m_cap.m_max_cpu_time_secs = t.count();
    return *this;
}

Capability Capability::Builder::build()
{
    return std::move(m_cap);
}

bool Capability::matchesPath(std::string_view prefix,
                             std::string_view path)
{
    if (path.starts_with(prefix)) return true;

    std::string normalized(path);
    if (!normalized.empty() && normalized.back() != '/')
        normalized += '/';

    return normalized.starts_with(prefix);
}

bool Capability::allowsRead(std::string_view path) const
{
    for (const auto& prefix : m_read_paths) {
        if (matchesPath(prefix, path)) return true;
    }
    return false;
}

bool Capability::allowsWrite(std::string_view path) const
{
    for (const auto& prefix : m_write_paths) {
        if (matchesPath(prefix, path)) return true;
    }
    return false;
}

} // namespace rook::adapters::security
