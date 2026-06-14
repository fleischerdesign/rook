#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "rook/sync/hlc.hpp"

namespace rook::sync {

template <typename T>
class AwSet {
public:
    struct ElementTag {
        T element;
        HlcTimestamp tag;
        std::string unique_id;

        bool operator==(const ElementTag& other) const
        {
            return unique_id == other.unique_id;
        }
    };

    struct ElementTagHash {
        std::size_t operator()(const ElementTag& et) const
        {
            return std::hash<std::string>{}(et.unique_id);
        }
    };

    void add(const T& element, const HlcTimestamp& ts)
    {
        std::string uid = ts.node_id + ":"
            + std::to_string(ts.wall_time_ms) + ":"
            + std::to_string(ts.logical_counter);

        ElementTag tag{element, ts, std::move(uid)};

        m_added.insert(std::move(tag));
    }

    void remove(const T& element)
    {
        for (auto it = m_added.begin(); it != m_added.end();) {
            if (it->element == element) {
                m_removed.insert(*it);
                it = m_added.erase(it);
            } else {
                ++it;
            }
        }
    }

    [[nodiscard]] std::vector<T> elements() const
    {
        std::vector<T> result;
        for (const auto& tag : m_added) {
            result.push_back(tag.element);
        }
        return result;
    }

    [[nodiscard]] bool contains(const T& element) const
    {
        for (const auto& tag : m_added) {
            if (tag.element == element)
                return true;
        }
        return false;
    }

    void merge(const AwSet& other)
    {
        for (const auto& tag : other.m_added) {
            if (m_removed.find(tag) != m_removed.end())
                continue;
            if (other.m_removed.find(tag) != other.m_removed.end())
                continue;
            m_added.insert(tag);
        }

        m_removed.insert(other.m_removed.begin(), other.m_removed.end());

        for (auto it = m_added.begin(); it != m_added.end();) {
            if (m_removed.find(*it) != m_removed.end())
                it = m_added.erase(it);
            else
                ++it;
        }
    }

    [[nodiscard]] std::size_t size() const { return m_added.size(); }
    [[nodiscard]] bool empty() const { return m_added.empty(); }

    void clear()
    {
        m_added.clear();
        m_removed.clear();
    }

    struct FullState {
        std::vector<ElementTag> added;
        std::vector<ElementTag> removed;
    };

    [[nodiscard]] FullState fullState() const
    {
        return {
            {m_added.begin(), m_added.end()},
            {m_removed.begin(), m_removed.end()},
        };
    }

    void addRaw(const ElementTag& tag) { m_added.insert(tag); }
    void removeRaw(const ElementTag& tag)
    {
        m_added.erase(tag);
        m_removed.insert(tag);
    }

private:
    std::unordered_set<ElementTag, ElementTagHash> m_added;
    std::unordered_set<ElementTag, ElementTagHash> m_removed;
};

} // namespace rook::sync
