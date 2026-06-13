#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <cstdint>

namespace rook::auth {

struct JwtPayload {
    std::string tenant_id;
    std::string issuer;
    int64_t expires_at = 0;
};

std::string generateToken(const JwtPayload& payload,
                          std::string_view secret);

std::optional<JwtPayload> verifyToken(std::string_view token,
                                       std::string_view secret);

} // namespace rook::auth
