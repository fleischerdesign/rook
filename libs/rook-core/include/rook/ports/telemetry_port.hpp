#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <chrono>

namespace rook::ports {

class TelemetryPort {
public:
    virtual ~TelemetryPort() = default;

    virtual void startSpan(std::string_view name) = 0;

    virtual void endSpan(std::string_view name) = 0;

    virtual void addAttribute(
        std::string_view span_name,
        std::string_view key,
        std::string_view value
    ) = 0;

    virtual void recordEvent(
        std::string_view span_name,
        std::string_view event_name
    ) = 0;

    virtual void recordException(
        std::string_view span_name,
        std::string_view message
    ) = 0;

    virtual void incrementCounter(std::string_view name, int64_t delta = 1) = 0;

    virtual void recordHistogram(std::string_view name, double value) = 0;

    virtual void flush() = 0;
};

} // namespace rook::ports
