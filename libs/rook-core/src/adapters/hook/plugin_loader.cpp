#include "rook/adapters/hook/plugin_loader.hpp"
#include "rook/adapters/hook/plugin_hook_adapter.hpp"

#include <dlfcn.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace rook::adapters::hook {

VersionMatch checkPluginVersion(const RookPluginInfo& plugin,
                                int host_major,
                                int host_minor)
{
    if (plugin.api_version_major != host_major)
        return VersionMatch::NeedsRecompile;
    if (plugin.api_version_minor > host_minor)
        return VersionMatch::TooNew;
    return VersionMatch::Compatible;
}

namespace {

bool isSharedLibrary(const std::string& filename)
{
    return filename.size() > 3
        && (filename.ends_with(".so")
            || filename.find(".so.") != std::string::npos);
}

} // namespace

std::vector<std::unique_ptr<ports::HookPort>>
PluginLoader::loadFromDirectory(std::string_view dir_path,
                                const RookCoreAPI& core_api)
{
    std::vector<std::unique_ptr<ports::HookPort>> result;

    DIR* dir = opendir(dir_path.data());
    if (!dir) {
        spdlog::info("PluginLoader: no plugin directory at {}", dir_path);
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (!isSharedLibrary(name)) continue;

        std::string full_path(dir_path);
        if (!full_path.empty() && full_path.back() != '/')
            full_path += '/';
        full_path += name;

        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        auto hook = loadSingle(full_path, core_api);
        if (hook) {
            result.push_back(std::move(hook));
        }
    }

    closedir(dir);
    return result;
}

std::unique_ptr<ports::HookPort>
PluginLoader::loadSingle(const std::string& path,
                         const RookCoreAPI& core_api)
{
    dlerror();

    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        spdlog::warn("PluginLoader: dlopen({}) failed: {}", path, dlerror());
        return nullptr;
    }

    auto* get_info = reinterpret_cast<const RookPluginInfo* (*)()>(
        dlsym(handle, "rook_plugin_get_info"));
    if (!get_info) {
        spdlog::warn("PluginLoader: {} missing rook_plugin_get_info: {}",
                     path, dlerror());
        dlclose(handle);
        return nullptr;
    }

    const RookPluginInfo* info_ptr = get_info();
    if (!info_ptr) {
        spdlog::warn("PluginLoader: {} rook_plugin_get_info returned null", path);
        dlclose(handle);
        return nullptr;
    }

    RookPluginInfo info = *info_ptr;

    if (std::string_view(info.category) != "hook") {
        spdlog::info("PluginLoader: {} has category '{}', skipping",
                     path, info.category ? info.category : "(null)");
        dlclose(handle);
        return nullptr;
    }

    auto match = checkPluginVersion(info,
        ROOK_PLUGIN_API_VERSION_MAJOR,
        ROOK_PLUGIN_API_VERSION_MINOR);

    if (match == VersionMatch::NeedsRecompile) {
        spdlog::warn("PluginLoader: {} api major version {} != host {} ({})",
                     path, info.api_version_major,
                     ROOK_PLUGIN_API_VERSION_MAJOR,
                     info.id ? info.id : "?");
        dlclose(handle);
        return nullptr;
    }

    if (match == VersionMatch::TooNew) {
        spdlog::warn("PluginLoader: {} api minor version {} > host {} ({})",
                     path, info.api_version_minor,
                     ROOK_PLUGIN_API_VERSION_MINOR,
                     info.id ? info.id : "?");
        dlclose(handle);
        return nullptr;
    }

    auto* create_fn = reinterpret_cast<void* (*)(const RookCoreAPI*)>(
        dlsym(handle, "rook_plugin_create"));
    if (!create_fn) {
        spdlog::warn("PluginLoader: {} missing rook_plugin_create: {}",
                     path, dlerror());
        dlclose(handle);
        return nullptr;
    }

    void* instance = create_fn(&core_api);
    if (!instance) {
        spdlog::warn("PluginLoader: {} rook_plugin_create returned null", path);
        dlclose(handle);
        return nullptr;
    }

    PluginFnTable fns{};
    fns.get_trigger_points = reinterpret_cast<const int* (*)(void*)>(
        dlsym(handle, "rook_hook_get_trigger_points"));
    fns.get_priority = reinterpret_cast<int (*)(void*)>(
        dlsym(handle, "rook_hook_get_priority"));
    fns.execute = reinterpret_cast<void (*)(void*, RookHookContext*)>(
        dlsym(handle, "rook_hook_execute"));
    fns.destroy = reinterpret_cast<void (*)(void*)>(
        dlsym(handle, "rook_plugin_destroy"));

    auto adapter = std::make_unique<PluginHookAdapter>(
        instance, info, fns, handle);

    spdlog::info("PluginLoader: loaded hook '{}' ({}) from {}",
                 adapter->name(), adapter->id(), path);

    return adapter;
}

} // namespace rook::adapters::hook
