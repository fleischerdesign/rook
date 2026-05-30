#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rook::ports {

struct ModelInfo {
    std::string id;
    std::string display_name;
};

class ModelDiscoveryPort {
public:
    virtual ~ModelDiscoveryPort() = default;
    virtual std::vector<ModelInfo> fetchModels(std::string_view api_key) = 0;
};

} // namespace rook::ports
