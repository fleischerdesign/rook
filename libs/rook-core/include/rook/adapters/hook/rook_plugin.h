#ifndef ROOK_PLUGIN_H
#define ROOK_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define ROOK_PLUGIN_API_VERSION_MAJOR 1
#define ROOK_PLUGIN_API_VERSION_MINOR 0
#define ROOK_PLUGIN_API_VERSION_PATCH 0

enum {
    ROOK_HOOK_PRE_USER_INPUT = 0,
    ROOK_HOOK_PRE_LLM,
    ROOK_HOOK_POST_LLM,
    ROOK_HOOK_PRE_TOOL_EXECUTION,
    ROOK_HOOK_POST_TOOL_EXECUTION,
    ROOK_HOOK_PRE_RESPONSE,
    ROOK_HOOK_ON_SYSTEM_STARTUP,
    ROOK_HOOK_ON_SYSTEM_SHUTDOWN
};

typedef struct {
    const char* id;
    const char* name;
    const char* description;
    const char* author;
    const char* version;
    const char* category;
    int api_version_major;
    int api_version_minor;
    int api_version_patch;
} RookPluginInfo;

typedef struct {
    void (*log_debug)(const char* msg);
    void (*log_info)(const char* msg);
    void (*log_warn)(const char* msg);
    void (*log_error)(const char* msg);
    const char* (*get_config_json)(void);
} RookCoreAPI;

typedef struct {
    int hook_point;
    const char* chat_id;
    const char* input;
    char* output;
    int output_capacity;
} RookHookContext;

const RookPluginInfo* rook_plugin_get_info(void);

void* rook_plugin_create(const RookCoreAPI* api);

void rook_plugin_destroy(void* instance);

const int* rook_hook_get_trigger_points(void* instance);

int rook_hook_get_priority(void* instance);

void rook_hook_execute(void* instance, RookHookContext* ctx);

#ifdef __cplusplus
}
#endif

#endif
