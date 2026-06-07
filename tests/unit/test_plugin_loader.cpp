#include <gtest/gtest.h>

#include "rook/adapters/hook/plugin_loader.hpp"
#include "rook/adapters/hook/plugin_hook_adapter.hpp"
#include "rook/adapters/hook/core_api_provider.hpp"
#include "rook/ports/hook_port.hpp"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

using namespace rook::adapters::hook;
using namespace rook::ports;

static std::string fixturesDir()
{
    const char* build = std::getenv("MESON_BUILD_ROOT");
    if (build) {
        return std::string(build) + "/tests/fixtures/plugins/";
    }

    const char* src = std::getenv("MESON_SOURCE_ROOT");
    if (src) {
        return std::string(src) + "/tests/fixtures/plugins/";
    }

    return "tests/fixtures/plugins/";
}

static void ensureFixturesBuilt()
{
    fs::path mock = fs::path(fixturesDir()) / "mock_hook.so";
    fs::path broken = fs::path(fixturesDir()) / "broken_hook.so";
    if (!fs::exists(mock) || !fs::exists(broken)) {
        GTEST_SKIP() << "Test fixtures not built - run: ninja -C build tests/fixtures/plugins/mock_hook.so";
    }
}

TEST(PluginLoaderTest, LoadValidHook)
{
    ensureFixturesBuilt();

    RookCoreAPI api = makeCoreAPI();
    PluginLoader loader;

    auto hooks = loader.loadFromDirectory(fixturesDir(), api);

    bool found = false;
    for (auto& h : hooks) {
        if (h->id() == "test.mock_hook") {
            found = true;
            EXPECT_EQ(h->name(), "Mock Hook");
            auto pts = h->triggerPoints();
            ASSERT_EQ(pts.size(), 1u);
            EXPECT_EQ(pts[0], HookPoint::PreResponse);
            EXPECT_EQ(h->priority(), 50);
        }
    }
    EXPECT_TRUE(found) << "Mock hook not found in loaded plugins";
}

TEST(PluginLoaderTest, BrokenVersionNotLoaded)
{
    ensureFixturesBuilt();

    RookCoreAPI api = makeCoreAPI();
    PluginLoader loader;

    auto hooks = loader.loadFromDirectory(fixturesDir(), api);

    for (auto& h : hooks) {
        EXPECT_NE(h->id(), "test.broken_hook")
            << "Broken version hook should not be loaded";
    }
}

TEST(PluginLoaderTest, ExecuteHookModifiesResponse)
{
    ensureFixturesBuilt();

    RookCoreAPI api = makeCoreAPI();
    PluginLoader loader;

    auto hooks = loader.loadFromDirectory(fixturesDir(), api);

    ASSERT_GE(hooks.size(), 1u) << "No hooks loaded";

    HookContext ctx{};
    ctx.point = HookPoint::PreResponse;
    ctx.chat_id = "test-chat";
    std::string response = "Hello world";
    ctx.response = &response;

    for (auto& h : hooks) {
        if (h->id() == "test.mock_hook") {
            h->execute(ctx);
        }
    }

    EXPECT_EQ(response, "Hello world [filtered]");
}

TEST(PluginLoaderTest, EmptyDirectoryReturnsEmpty)
{
    RookCoreAPI api = makeCoreAPI();
    PluginLoader loader;

    auto hooks = loader.loadFromDirectory("/nonexistent/path/for/plugins", api);

    EXPECT_TRUE(hooks.empty());
}

TEST(PluginHookAdapterTest, TriggerPointsFromCFn)
{
    ensureFixturesBuilt();

    RookCoreAPI api = makeCoreAPI();
    PluginLoader loader;
    auto hooks = loader.loadFromDirectory(fixturesDir(), api);

    for (auto& h : hooks) {
        if (h->id() == "test.mock_hook") {
            auto pts = h->triggerPoints();
            EXPECT_FALSE(pts.empty());
            bool found_pre_response = false;
            for (auto pt : pts) {
                if (pt == HookPoint::PreResponse)
                    found_pre_response = true;
            }
            EXPECT_TRUE(found_pre_response)
                << "Mock hook should trigger on PreResponse";
        }
    }
}

TEST(PluginHookAdapterTest, IdAndNameFromInfo)
{
    ensureFixturesBuilt();

    RookCoreAPI api = makeCoreAPI();
    PluginLoader loader;
    auto hooks = loader.loadFromDirectory(fixturesDir(), api);

    for (auto& h : hooks) {
        EXPECT_FALSE(h->id().empty());
        EXPECT_FALSE(h->name().empty());
    }
}

TEST(VersionCheckTest, AllCases)
{
    RookPluginInfo info{};
    info.api_version_major = 1;
    info.api_version_minor = 0;

    EXPECT_EQ(checkPluginVersion(info, 1, 0), VersionMatch::Compatible);
    EXPECT_EQ(checkPluginVersion(info, 1, 5), VersionMatch::Compatible);

    info.api_version_major = 2;
    EXPECT_EQ(checkPluginVersion(info, 1, 5), VersionMatch::NeedsRecompile);

    info.api_version_major = 1;
    info.api_version_minor = 5;
    EXPECT_EQ(checkPluginVersion(info, 1, 3), VersionMatch::TooNew);
}
