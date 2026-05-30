#include "rook/adapters/model/model_cache.hpp"
#include <spdlog/spdlog.h>

namespace rook::adapters::model {

ModelCache& ModelCache::instance() {
    static ModelCache cache;
    return cache;
}

void ModelCache::store(std::string_view provider_id, std::vector<ports::ModelInfo> models) {
    std::lock_guard lock(m_mutex);
    std::erase_if(m_cache, [id = std::string(provider_id)](const auto& p) {
        return p.first == id;
    });
    m_cache.emplace_back(provider_id, std::move(models));
    spdlog::info("ModelCache: stored {} models for provider {}", m_cache.back().second.size(), provider_id);
}

std::vector<ports::ModelInfo> ModelCache::getAllEnabled(
    const std::function<bool(std::string_view)>& is_enabled
) const {
    std::lock_guard lock(m_mutex);
    std::vector<ports::ModelInfo> result;
    for (const auto& [prov_id, models] : m_cache) {
        if (!is_enabled(prov_id)) continue;
        result.insert(result.end(), models.begin(), models.end());
    }
    return result;
}

} // namespace rook::adapters::model
