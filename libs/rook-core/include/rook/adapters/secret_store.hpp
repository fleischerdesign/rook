#pragma once

#include <string>
#include <string_view>

namespace rook::adapters {

class SecretStore {
public:
    SecretStore(std::string app_id);

    void store(std::string_view key, std::string_view value);
    std::string load(std::string_view key) const;
    void remove(std::string_view key);

private:
    std::string m_app_id;
};

} // namespace rook::adapters
