#include <gtest/gtest.h>
#include "rook/adapters/security/security_manager.hpp"
#include "rook/ports/tool_port.hpp"

using namespace rook::adapters::security;

static std::string configJson()
{
    return R"([
        {
            "id": "filesystem",
            "capabilities": {
                "read": ["/home/user"],
                "write": ["/tmp"],
                "network": false,
                "max_memory_mb": 256,
                "max_cpu_time_secs": 60
            }
        },
        {
            "id": "websearch",
            "capabilities": {
                "read": [],
                "write": [],
                "network": true
            }
        }
    ])";
}

TEST(SecurityManagerTest, LoadFromConfigLoadsCapabilities)
{
    SecurityManager mgr;
    mgr.loadFromConfig(configJson());

    auto* cap = mgr.findCapability("filesystem");
    ASSERT_NE(cap, nullptr);
    EXPECT_TRUE(cap->allowsRead("/home/user/projects"));
    EXPECT_TRUE(cap->allowsWrite("/tmp/output"));
    EXPECT_FALSE(cap->allowsNetwork());

    auto* cap2 = mgr.findCapability("websearch");
    ASSERT_NE(cap2, nullptr);
    EXPECT_TRUE(cap2->allowsNetwork());
}

TEST(SecurityManagerTest, UnknownServerIsAllowed)
{
    SecurityManager mgr;
    mgr.loadFromConfig(configJson());

    rook::ports::ToolCall call;
    call.name = "read_file";
    call.arguments = R"({"path": "/nonexistent"})";

    EXPECT_TRUE(mgr.isAllowed("unknown", call));
}

TEST(SecurityManagerTest, ReadFileAllowed)
{
    SecurityManager mgr;
    mgr.loadFromConfig(configJson());

    rook::ports::ToolCall call;
    call.name = "read_file";
    call.arguments = R"({"path": "/home/user/notes.txt"})";

    EXPECT_TRUE(mgr.isAllowed("filesystem", call));
}

TEST(SecurityManagerTest, ReadFileDenied)
{
    SecurityManager mgr;
    mgr.loadFromConfig(configJson());

    rook::ports::ToolCall call;
    call.name = "read_file";
    call.arguments = R"({"path": "/etc/shadow"})";

    EXPECT_FALSE(mgr.isAllowed("filesystem", call));
}

TEST(SecurityManagerTest, WriteFileAllowed)
{
    SecurityManager mgr;
    mgr.loadFromConfig(configJson());

    rook::ports::ToolCall call;
    call.name = "write_file";
    call.arguments = R"({"path": "/tmp/output.txt"})";

    EXPECT_TRUE(mgr.isAllowed("filesystem", call));
}

TEST(SecurityManagerTest, WriteFileDenied)
{
    SecurityManager mgr;
    mgr.loadFromConfig(configJson());

    rook::ports::ToolCall call;
    call.name = "write_file";
    call.arguments = R"({"path": "/home/user/write_here.txt"})";

    EXPECT_FALSE(mgr.isAllowed("filesystem", call));
}

TEST(SecurityManagerTest, ListDirectoryAllowed)
{
    SecurityManager mgr;
    mgr.loadFromConfig(configJson());

    rook::ports::ToolCall call;
    call.name = "list_directory";
    call.arguments = R"({"path": "/home/user"})";

    EXPECT_TRUE(mgr.isAllowed("filesystem", call));
}

TEST(SecurityManagerTest, EmptyConfigDoesNothing)
{
    SecurityManager mgr;
    mgr.loadFromConfig("");

    auto* cap = mgr.findCapability("anything");
    EXPECT_EQ(cap, nullptr);
}

TEST(SecurityManagerTest, InvalidJsonDoesNotCrash)
{
    SecurityManager mgr;
    mgr.loadFromConfig("{bad");

    auto* cap = mgr.findCapability("anything");
    EXPECT_EQ(cap, nullptr);
}
