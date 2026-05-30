#pragma once

#include <memory>
#include "rook/ports/llm_port.hpp"

namespace rook::adapters::llm {

std::unique_ptr<ports::LlmPort> makeOpenAiAdapter();
std::unique_ptr<ports::LlmPort> makeDeepSeekAdapter();
std::unique_ptr<ports::LlmPort> makeOllamaAdapter();
std::unique_ptr<ports::LlmPort> makeAnthropicAdapter();

} // namespace rook::adapters::llm
