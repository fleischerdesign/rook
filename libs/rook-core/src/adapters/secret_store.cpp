#include "rook/adapters/secret_store.hpp"
#include <libsecret/secret.h>
#include <spdlog/spdlog.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

namespace rook::adapters {

static const SecretSchema k_secret_schema = {
    "io.github.fleischerdesign.Rook.ApiKey",
    SECRET_SCHEMA_NONE,
    {
        {"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
        {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
    },
};

#pragma GCC diagnostic pop

SecretStore::SecretStore(std::string app_id)
    : m_app_id(std::move(app_id))
{}

void SecretStore::store(std::string_view key, std::string_view value) {
    GError* error = nullptr;

    secret_password_store_sync(
        &k_secret_schema,
        SECRET_COLLECTION_DEFAULT,
        (m_app_id + " - API Key").c_str(),
        value.data(),
        nullptr,
        &error,
        "key", key.data(),
        nullptr
    );

    if (error) {
        spdlog::error("Failed to store secret for {}: {}", key, error->message);
        g_error_free(error);
    }
}

std::string SecretStore::load(std::string_view key) const {
    GError* error = nullptr;

    auto* result = secret_password_lookup_sync(
        &k_secret_schema,
        nullptr,
        &error,
        "key", key.data(),
        nullptr
    );

    if (error) {
        spdlog::error("Failed to load secret for {}: {}", key, error->message);
        g_error_free(error);
        return {};
    }

    if (result) {
        std::string value(result);
        secret_password_free(result);
        return value;
    }

    return {};
}

void SecretStore::remove(std::string_view key) {
    GError* error = nullptr;

    secret_password_clear_sync(
        &k_secret_schema,
        nullptr,
        &error,
        "key", key.data(),
        nullptr
    );

    if (error) {
        spdlog::error("Failed to remove secret for {}: {}", key, error->message);
        g_error_free(error);
    }
}

} // namespace rook::adapters
