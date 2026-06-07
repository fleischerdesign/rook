#include "rook/adapters/hook/core_api_provider.hpp"

#include <memory>
#include <spdlog/spdlog.h>

namespace rook::adapters::hook {

namespace {

void log_debug_c(const char* msg) { spdlog::debug("{}", msg); }
void log_info_c(const char* msg)  { spdlog::info("{}", msg); }
void log_warn_c(const char* msg)  { spdlog::warn("{}", msg); }
void log_error_c(const char* msg) { spdlog::error("{}", msg); }

static std::shared_ptr<std::string> g_config_json;

const char* get_config_json_c(void)
{
    return g_config_json ? g_config_json->c_str() : "{}";
}

} // namespace

RookCoreAPI makeCoreAPI(std::string config_json)
{
    g_config_json = std::make_shared<std::string>(std::move(config_json));

    RookCoreAPI api{};
    api.log_debug = log_debug_c;
    api.log_info  = log_info_c;
    api.log_warn  = log_warn_c;
    api.log_error = log_error_c;
    api.get_config_json = get_config_json_c;
    return api;
}

} // namespace rook::adapters::hook
