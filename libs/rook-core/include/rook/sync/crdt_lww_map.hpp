#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <vector>
#include <utility>
#include <cstdint>
#include "rook/sync/hlc.hpp"

namespace rook::sync {

template <typename K, typename V>
class LwwMap {
public:
    void put(const K& key, V value, const HlcTimestamp& ts)
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end() || isLater(ts, it->second.timestamp)) {
            m_entries[key] = Entry{std::move(value), ts, false};
        }
    }

    void remove(const K& key, const HlcTimestamp& ts)
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end() || isLater(ts, it->second.timestamp)) {
            m_entries[key] = Entry{V{}, ts, true};
        }
    }

    [[nodiscard]] std::optional<V> get(const K& key) const
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end() || it->second.tombstone)
            return std::nullopt;
        return it->second.value;
    }

    [[nodiscard]] std::vector<std::pair<K, V>> snapshot() const
    {
        std::vector<std::pair<K, V>> result;
        for (const auto& [key, entry] : m_entries) {
            if (!entry.tombstone)
                result.emplace_back(key, entry.value);
        }
        return result;
    }

    void merge(const LwwMap& other)
    {
        for (const auto& [key, entry] : other.m_entries) {
            auto it = m_entries.find(key);
            if (it == m_entries.end()) {
                m_entries[key] = entry;
            } else if (isLater(entry.timestamp, it->second.timestamp)) {
                m_entries[key] = entry;
            }
        }
    }

    void clear() { m_entries.clear(); }
    [[nodiscard]] bool empty() const { return m_entries.empty(); }
    [[nodiscard]] std::size_t size() const
    {
        std::size_t count = 0;
        for (const auto& [_, e] : m_entries)
            if (!e.tombstone) ++count;
        return count;
    }

    struct FullEntry {
        K key;
        V value;
        HlcTimestamp timestamp;
        bool tombstone;
    };

    [[nodiscard]] std::vector<FullEntry> allEntries() const
    {
        std::vector<FullEntry> result;
        for (const auto& [key, entry] : m_entries) {
            result.push_back({key, entry.value, entry.timestamp, entry.tombstone});
        }
        return result;
    }

    void putRaw(const K& key, V value, const HlcTimestamp& ts, bool tombstone)
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end() || isLater(ts, it->second.timestamp)) {
            m_entries[key] = Entry{std::move(value), ts, tombstone};
        }
    }

private:
    struct Entry {
        V value;
        HlcTimestamp timestamp;
        bool tombstone = false;
    };

    static bool isLater(const HlcTimestamp& a, const HlcTimestamp& b)
    {
        if (a.wall_time_ms != b.wall_time_ms)
            return a.wall_time_ms > b.wall_time_ms;
        if (a.logical_counter != b.logical_counter)
            return a.logical_counter > b.logical_counter;
        return a.node_id > b.node_id;
    }

    std::unordered_map<K, Entry> m_entries;
};

} // namespace rook::sync
