#pragma once

#include <memory>
#include <string_view>
#include "rook/ports/model_discovery_port.hpp"

namespace rook::adapters::model {

std::unique_ptr<ports::ModelDiscoveryPort> makeOllamaDiscovery(std::string_view base_url);
std::unique_ptr<ports::ModelDiscoveryPort> makeOpenAiDiscovery(std::string_view base_url);
std::unique_ptr<ports::ModelDiscoveryPort> makeAnthropicDiscovery();

} // namespace rook::adapters::model
