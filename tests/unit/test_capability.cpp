#include <gtest/gtest.h>
#include "rook/adapters/security/capability.hpp"

using namespace rook::adapters::security;

TEST(CapabilityTest, BuilderReadPaths)
{
    auto cap = Capability::grant()
        .read("/home/user/projects")
        .read("/home/user/docs")
        .build();

    EXPECT_TRUE(cap.allowsRead("/home/user/projects/src/main.cpp"));
    EXPECT_TRUE(cap.allowsRead("/home/user/docs/"));
    EXPECT_TRUE(cap.allowsRead("/home/user/projects/"));
    EXPECT_FALSE(cap.allowsRead("/etc/passwd"));
    EXPECT_FALSE(cap.allowsRead("/home/user/../../etc/shadow"));
}

TEST(CapabilityTest, BuilderWritePaths)
{
    auto cap = Capability::grant()
        .write("/tmp/output")
        .build();

    EXPECT_TRUE(cap.allowsWrite("/tmp/output/result.txt"));
    EXPECT_TRUE(cap.allowsWrite("/tmp/output"));
    EXPECT_FALSE(cap.allowsWrite("/tmp/other"));
}

TEST(CapabilityTest, ExactPath)
{
    auto cap = Capability::grant()
        .read("/tmp")
        .build();

    EXPECT_TRUE(cap.allowsRead("/tmp"));
    EXPECT_TRUE(cap.allowsRead("/tmp/foo"));
}

TEST(CapabilityTest, NetworkFlag)
{
    auto cap_off = Capability::grant().noNetwork().build();
    EXPECT_FALSE(cap_off.allowsNetwork());

    auto cap_on = Capability::grant().allowNetwork().build();
    EXPECT_TRUE(cap_on.allowsNetwork());
}

TEST(CapabilityTest, ResourceLimits)
{
    auto cap = Capability::grant()
        .maxMemoryMb(512)
        .maxCpuTime(std::chrono::seconds(120))
        .build();

    EXPECT_EQ(cap.maxMemoryMb(), 512);
    EXPECT_EQ(cap.maxCpuTimeSecs(), 120);
}

TEST(CapabilityTest, DefaultValues)
{
    auto cap = Capability::grant().build();

    EXPECT_FALSE(cap.allowsNetwork());
    EXPECT_EQ(cap.maxMemoryMb(), 256);
    EXPECT_EQ(cap.maxCpuTimeSecs(), 60);
    EXPECT_TRUE(cap.readPaths().empty());
    EXPECT_TRUE(cap.writePaths().empty());
    EXPECT_FALSE(cap.allowsRead("/any/path"));
    EXPECT_FALSE(cap.allowsWrite("/any/path"));
}

TEST(CapabilityTest, TraitsAppended)
{
    auto cap = Capability::grant()
        .read("/a")
        .read("/b")
        .build();

    EXPECT_EQ(cap.readPaths().size(), 2u);
}
