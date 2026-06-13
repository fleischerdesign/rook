#pragma once

#include "rook/ports/telemetry_port.hpp"

namespace rook::adapters::telemetry {

class NullTelemetryPort : public rook::ports::TelemetryPort {
public:
    void startSpan(std::string_view) override {}
    void endSpan(std::string_view) override {}
    void addAttribute(std::string_view, std::string_view, std::string_view) override {}
    void recordEvent(std::string_view, std::string_view) override {}
    void recordException(std::string_view, std::string_view) override {}
    void incrementCounter(std::string_view, int64_t) override {}
    void recordHistogram(std::string_view, double) override {}
    void flush() override {}
};

} // namespace rook::adapters::telemetry
