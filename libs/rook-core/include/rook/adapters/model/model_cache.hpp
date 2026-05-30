#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include "rook/ports/model_discovery_port.hpp"

namespace rook::adapters::model {

class ModelCache {
public:
    static ModelCache& instance();

    void store(std::string_view provider_id, std::vector<ports::ModelInfo> models);
    std::vector<ports::ModelInfo> getAllEnabled(
        const std::function<bool(std::string_view)>& is_enabled) const;

private:
    ModelCache() = default;
    mutable std::mutex m_mutex;
    std::vector<std::pair<std::string, std::vector<ports::ModelInfo>>> m_cache;
};

} // namespace rook::adapters::model
