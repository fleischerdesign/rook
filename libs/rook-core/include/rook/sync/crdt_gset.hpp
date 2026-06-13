#pragma once

#include <unordered_set>
#include <vector>

namespace rook::sync {

template <typename T>
class GSet {
public:
    void add(const T& element) { m_set.insert(element); }
    void add(T&& element) { m_set.insert(std::move(element)); }

    [[nodiscard]] bool contains(const T& element) const
    {
        return m_set.find(element) != m_set.end();
    }

    [[nodiscard]] std::size_t size() const { return m_set.size(); }
    [[nodiscard]] bool empty() const { return m_set.empty(); }

    [[nodiscard]] std::vector<T> elements() const
    {
        return {m_set.begin(), m_set.end()};
    }

    void merge(const GSet& other)
    {
        m_set.insert(other.m_set.begin(), other.m_set.end());
    }

private:
    std::unordered_set<T> m_set;
};

} // namespace rook::sync
