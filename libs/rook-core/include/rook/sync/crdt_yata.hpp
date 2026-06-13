#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <compare>
#include <cstdint>
#include "rook/sync/hlc.hpp"

namespace rook::sync {

template <typename T>
class YataSequence {
public:
    static constexpr const char* kBegin = "__BEGIN__";
    static constexpr const char* kEnd   = "__END__";

    void insert(std::string_view left_origin,
                std::string_view right_origin,
                T value,
                const HlcTimestamp& ts)
    {
        std::string id = ts.node_id + ":"
            + std::to_string(ts.wall_time_ms) + ":"
            + std::to_string(ts.logical_counter);

        Element elem;
        elem.id = id;
        elem.value = std::move(value);
        elem.left_origin = std::string(left_origin.empty() ? kBegin : left_origin);
        elem.right_origin = std::string(right_origin.empty() ? kEnd : right_origin);

        m_elements[std::move(id)] = std::move(elem);
    }

    void remove(std::string_view id)
    {
        auto it = m_elements.find(std::string(id));
        if (it != m_elements.end())
            it->second.tombstone = true;
    }

    [[nodiscard]] std::vector<T> snapshot() const
    {
        auto order = computeOrder();
        std::vector<T> result;
        for (const auto& id : order) {
            if (id == kBegin || id == kEnd) continue;
            auto it = m_elements.find(id);
            if (it != m_elements.end() && !it->second.tombstone)
                result.push_back(it->second.value);
        }
        return result;
    }

    void merge(const YataSequence& other)
    {
        for (const auto& [id, elem] : other.m_elements) {
            auto it = m_elements.find(id);
            if (it == m_elements.end()) {
                m_elements[id] = elem;
            } else {
                if (elem.tombstone)
                    it->second.tombstone = true;
            }
        }
    }

    [[nodiscard]] bool contains(std::string_view id) const
    {
        return m_elements.find(std::string(id)) != m_elements.end();
    }

    [[nodiscard]] std::size_t live_size() const
    {
        std::size_t count = 0;
        for (const auto& [_, e] : m_elements)
            if (!e.tombstone) ++count;
        return count;
    }

    [[nodiscard]] bool empty() const
    {
        for (const auto& [_, e] : m_elements)
            if (!e.tombstone) return false;
        return true;
    }

    void clear() { m_elements.clear(); }

private:
    struct Element {
        std::string id;
        T value;
        std::string left_origin;
        std::string right_origin;
        bool tombstone = false;
    };

    [[nodiscard]] std::vector<std::string> computeOrder() const
    {
        std::unordered_map<std::string, std::vector<std::string>> successors;
        std::unordered_map<std::string, int> in_degree;
        std::unordered_set<std::string> all_nodes;

        all_nodes.insert(kBegin);
        all_nodes.insert(kEnd);

        for (const auto& [id, elem] : m_elements) {
            all_nodes.insert(id);

            std::string left = elem.left_origin.empty() ? kBegin : elem.left_origin;
            std::string right = elem.right_origin.empty() ? kEnd : elem.right_origin;

            successors[left].push_back(id);
            successors[id].push_back(right);

            all_nodes.insert(left);
            all_nodes.insert(right);
        }

        for (const auto& node : all_nodes) {
            in_degree[node] = 0;
        }

        for (const auto& [node, succs] : successors) {
            for (const auto& s : succs) {
                auto it = in_degree.find(s);
                if (it != in_degree.end())
                    ++in_degree[s];
            }
        }

        using QueueEntry = std::pair<std::string, int>;
        auto cmp = [](const QueueEntry& a, const QueueEntry& b) {
            return a.first > b.first;
        };

        std::priority_queue<QueueEntry,
            std::vector<QueueEntry>,
            decltype(cmp)> ready(cmp);

        for (const auto& [node, deg] : in_degree) {
            if (deg == 0) {
                ready.emplace(node, 0);
            }
        }

        std::vector<std::string> order;
        std::unordered_set<std::string> emitted;

        while (!ready.empty()) {
            auto [node, _] = ready.top();
            ready.pop();

            if (emitted.count(node) > 0)
                continue;

            order.push_back(node);
            emitted.insert(node);

            auto sit = successors.find(node);
            if (sit != successors.end()) {
                for (const auto& succ : sit->second) {
                    auto dit = in_degree.find(succ);
                    if (dit != in_degree.end()) {
                        --dit->second;
                        if (dit->second == 0)
                            ready.emplace(succ, 0);
                    }
                }
            }
        }

        return order;
    }

    std::unordered_map<std::string, Element> m_elements;
};

} // namespace rook::sync
