#pragma once

#include <string>
#include "rook/ports/llm_port.hpp"
#include "rook/ports/store_port.hpp"

namespace rook::adapters {
class SecretStore;
}

namespace rook::core {

class SettingsLoader {
public:
    bool load(ports::StorePort& store, ports::LlmPort& llm,
              rook::adapters::SecretStore& secrets);
    void save(ports::StorePort& store, const ports::LlmPort& llm,
              rook::adapters::SecretStore& secrets);
};

} // namespace rook::core
