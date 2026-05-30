#pragma once

#include <memory>
#include <string>
#include "rook/ports/store_port.hpp"

namespace rook::adapters::store {

std::unique_ptr<ports::StorePort> makeJsonStore(const std::string& base_dir);

} // namespace rook::adapters::store
