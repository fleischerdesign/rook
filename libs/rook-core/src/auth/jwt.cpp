#include "rook/auth/jwt.hpp"
#include <openssl/hmac.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>

namespace rook::auth {

static const std::string kBase64urlChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string base64urlEncode(std::string_view data)
{
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    int val = 0;
    int bits = -6;

    for (unsigned char c : data) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out += kBase64urlChars[(val >> bits) & 0x3F];
            bits -= 6;
        }
    }

    if (bits > -6)
        out += kBase64urlChars[((val << 8) >> (bits + 8)) & 0x3F];

    while (out.size() % 4)
        out += '=';

    out.erase(std::remove(out.begin(), out.end(), '='), out.end());

    return out;
}

static std::string base64urlDecode(std::string_view data)
{
    std::string s(data);
    while (s.size() % 4)
        s += '=';

    std::string out;
    out.reserve((s.size() / 4) * 3);

    int val = 0;
    int bits = -8;

    for (unsigned char c : s) {
        if (c == '=') break;
        auto pos = kBase64urlChars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            out += static_cast<char>((val >> bits) & 0xFF);
            bits -= 8;
        }
    }

    return out;
}

static std::string hmacSha256(std::string_view data,
                               std::string_view key)
{
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(),
         result, &len);

    return {reinterpret_cast<char*>(result), len};
}

std::string generateToken(const JwtPayload& payload,
                          std::string_view secret)
{
    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    nlohmann::json body;
    body["tenant_id"] = payload.tenant_id;
    body["iss"] = payload.issuer;
    body["exp"] = payload.expires_at;

    auto header_b64 = base64urlEncode(header.dump());
    auto payload_b64 = base64urlEncode(body.dump());

    auto signature = base64urlEncode(
        hmacSha256(header_b64 + "." + payload_b64, secret));

    return header_b64 + "." + payload_b64 + "." + signature;
}

std::optional<JwtPayload> verifyToken(std::string_view token,
                                       std::string_view secret)
{
    auto first_dot = token.find('.');
    if (first_dot == std::string::npos) return std::nullopt;

    auto second_dot = token.find('.', first_dot + 1);
    if (second_dot == std::string::npos) return std::nullopt;

    auto header_b64 = token.substr(0, first_dot);
    auto payload_b64 = token.substr(first_dot + 1,
        second_dot - first_dot - 1);
    auto signature_b64 = token.substr(second_dot + 1);

    auto expected_sig = base64urlEncode(
        hmacSha256(std::string(header_b64) + "." + std::string(payload_b64),
                   secret));

    if (expected_sig != signature_b64)
        return std::nullopt;

    auto payload_json = base64urlDecode(payload_b64);
    try {
        auto j = nlohmann::json::parse(payload_json);
        JwtPayload p;
        p.tenant_id = j.at("tenant_id").get<std::string>();
        p.issuer = j.value("iss", "");
        p.expires_at = j.value("exp", 0LL);
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace rook::auth
