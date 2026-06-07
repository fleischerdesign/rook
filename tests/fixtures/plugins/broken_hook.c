#include "rook_plugin.h"

static int g_trigger_points[] = {ROOK_HOOK_PRE_RESPONSE, -1};

static RookPluginInfo g_info = {
    "test.broken_hook",
    "Broken Hook",
    "Has incompatible API version",
    "Rook Test Suite",
    "1.0.0",
    "hook",
    99,
    0,
    0
};

const RookPluginInfo* rook_plugin_get_info(void)
{
    return &g_info;
}

void* rook_plugin_create(const RookCoreAPI* api)
{
    (void)api;
    return (void*)1;
}

void rook_plugin_destroy(void* instance)
{
    (void)instance;
}

const int* rook_hook_get_trigger_points(void* instance)
{
    (void)instance;
    return g_trigger_points;
}

int rook_hook_get_priority(void* instance)
{
    (void)instance;
    return 0;
}

void rook_hook_execute(void* instance, RookHookContext* ctx)
{
    (void)instance;
    (void)ctx;
}
