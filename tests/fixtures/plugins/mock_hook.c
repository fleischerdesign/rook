#include "rook_plugin.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const RookCoreAPI* g_api = NULL;

static int g_trigger_points[] = {ROOK_HOOK_PRE_RESPONSE, -1};

static RookPluginInfo g_info = {
    "test.mock_hook",
    "Mock Hook",
    "A test hook that appends [filtered] to responses",
    "Rook Test Suite",
    "1.0.0",
    "hook",
    ROOK_PLUGIN_API_VERSION_MAJOR,
    ROOK_PLUGIN_API_VERSION_MINOR,
    ROOK_PLUGIN_API_VERSION_PATCH
};

const RookPluginInfo* rook_plugin_get_info(void)
{
    return &g_info;
}

void* rook_plugin_create(const RookCoreAPI* api)
{
    g_api = api;
    if (g_api && g_api->log_info)
        g_api->log_info("mock_hook: created");

    return (void*)1;
}

void rook_plugin_destroy(void* instance)
{
    (void)instance;
    if (g_api && g_api->log_info)
        g_api->log_info("mock_hook: destroyed");
}

const int* rook_hook_get_trigger_points(void* instance)
{
    (void)instance;
    return g_trigger_points;
}

int rook_hook_get_priority(void* instance)
{
    (void)instance;
    return 50;
}

void rook_hook_execute(void* instance, RookHookContext* ctx)
{
    (void)instance;
    if (!ctx || !ctx->output || ctx->output_capacity <= 0) return;
    if (!ctx->input) return;

    const char* key = "\"response\":\"";
    const char* pos = strstr(ctx->input, key);
    if (!pos) return;

    const char* val_start = pos + strlen(key);
    const char* val_end = strchr(val_start, '\"');
    if (!val_end) return;

    size_t prefix_len = (size_t)(val_start - ctx->input);
    size_t val_len = (size_t)(val_end - val_start);
    size_t suffix_off = (size_t)(val_end - ctx->input);

    int written = snprintf(ctx->output, (size_t)ctx->output_capacity,
        "%.*s%.*s%s%s",
        (int)prefix_len, ctx->input,
        (int)val_len,    val_start,
        " [filtered]",
        ctx->input + suffix_off);

    if (written < 0 || written >= ctx->output_capacity) {
        snprintf(ctx->output, (size_t)ctx->output_capacity, "%s", ctx->input);
    }
}
