#pragma once

#include "rook/ports/tool_port.hpp"

#include <memory>

namespace rook::adapters::mcp {

std::unique_ptr<rook::ports::ToolPort> makeNullToolPort();

} // namespace rook::adapters::mcp
