#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace rook::ports {

class WakewordPort {
public:
    virtual ~WakewordPort() = default;

    virtual std::string id() const = 0;
    virtual std::string engineName() const = 0;

    virtual bool isReady() const = 0;
    virtual bool needsKey() const = 0;
    virtual std::size_t frameSize() const = 0;

    virtual bool processFrame(const int16_t* pcm) = 0;
    virtual void reset() = 0;
    virtual void setSensitivity(float value) = 0;
};

} // namespace rook::ports
