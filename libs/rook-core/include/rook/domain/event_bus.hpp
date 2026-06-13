#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <algorithm>
#include <variant>
#include "rook/domain/events.hpp"
#include "rook/ports/telemetry_port.hpp"

namespace rook::domain {

class EventBus {
public:
    using HandlerId = std::size_t;

    void setTelemetry(rook::ports::TelemetryPort* telemetry)
    {
        m_telemetry = telemetry;
    }

    template <typename T>
    HandlerId subscribe(std::function<void(const T&)> handler) {
        std::unique_lock lock(m_mutex);
        auto id = m_next_id++;
        m_handlers[typeid(T)].emplace_back(id,
            [h = std::move(handler)](const void* ptr) {
                h(*static_cast<const T*>(ptr));
            });
        return id;
    }

    void publish(const DomainEvent& event) {
        if (m_telemetry) {
            std::visit([this](const auto& concrete) {
                m_telemetry->startSpan(typeid(decltype(concrete)).name());
            }, event);
        }

        if (m_telemetry)
            m_telemetry->incrementCounter("event_bus.publish", 1);

        std::vector<std::function<void(const void*)>> handlers;
        {
            std::shared_lock lock(m_mutex);
            std::visit([this, &handlers](const auto& concrete) {
                using T = std::decay_t<decltype(concrete)>;
                auto it = m_handlers.find(typeid(T));
                if (it != m_handlers.end()) {
                    for (const auto& entry : it->second) {
                        handlers.push_back(entry.callback);
                    }
                }
            }, event);
        }
        std::visit([&handlers](const auto& concrete) {
            for (auto& fn : handlers) {
                fn(&concrete);
            }
        }, event);

        if (m_telemetry) {
            std::visit([this](const auto& concrete) {
                m_telemetry->endSpan(typeid(decltype(concrete)).name());
            }, event);
        }
    }

    void unsubscribe(HandlerId id) {
        std::unique_lock lock(m_mutex);
        for (auto& [type, handlers] : m_handlers) {
            auto it = std::remove_if(handlers.begin(), handlers.end(),
                [id](const HandlerEntry& entry) { return entry.id == id; });
            handlers.erase(it, handlers.end());
        }
    }

private:
    struct HandlerEntry {
        HandlerId id;
        std::function<void(const void*)> callback;
    };

    std::shared_mutex m_mutex;
    HandlerId m_next_id = 0;
    std::unordered_map<
        std::type_index,
        std::vector<HandlerEntry>
    > m_handlers;
    rook::ports::TelemetryPort* m_telemetry = nullptr;
};

} // namespace rook::domain
