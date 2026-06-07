#include <gtest/gtest.h>
#include "rook/adapters/extension/extension_manifest.hpp"

using namespace rook::adapters::extension;

static std::string validManifest()
{
    return R"JSON({
        "name": "test-extension",
        "display_name": "Test Extension",
        "version": "1.0.0",
        "description": "A test extension",
        "author": "Test Author",
        "license": "MIT",
        "homepage": "https://example.com",
        "mcp_servers": [
            {
                "id": "test-server",
                "command": "echo",
                "args": ["hello"],
                "enabled": true
            },
            {
                "id": "another-server",
                "command": "python3",
                "args": ["-c", "print(1)"],
                "enabled": false
            }
        ],
        "skills": [
            {
                "name": "code-review",
                "description": "Reviews code",
                "prompt": "You are a code reviewer."
            }
        ],
        "commands": [
            {
                "name": "review",
                "description": "Review current file",
                "prompt": "Review this code."
            }
        ],
        "context_files": [
            {
                "path": "docs/*.md",
                "description": "Documentation files"
            }
        ]
    })JSON";
}

TEST(ExtensionManifestTest, ParseValidManifest)
{
    auto m = parseManifest(validManifest());

    EXPECT_TRUE(m.valid);
    EXPECT_EQ(m.error, "");
    EXPECT_EQ(m.name, "test-extension");
    EXPECT_EQ(m.display_name, "Test Extension");
    EXPECT_EQ(m.version, "1.0.0");
    EXPECT_EQ(m.description, "A test extension");
    EXPECT_EQ(m.author, "Test Author");
    EXPECT_EQ(m.license, "MIT");
    EXPECT_EQ(m.homepage, "https://example.com");
}

TEST(ExtensionManifestTest, ParseMcpServers)
{
    auto m = parseManifest(validManifest());

    ASSERT_EQ(m.mcp_servers.size(), 2u);
    EXPECT_EQ(m.mcp_servers[0].id, "test-server");
    EXPECT_EQ(m.mcp_servers[0].command, "echo");
    ASSERT_EQ(m.mcp_servers[0].args.size(), 1u);
    EXPECT_EQ(m.mcp_servers[0].args[0], "hello");
    EXPECT_TRUE(m.mcp_servers[0].enabled);

    EXPECT_EQ(m.mcp_servers[1].id, "another-server");
    EXPECT_EQ(m.mcp_servers[1].command, "python3");
    ASSERT_EQ(m.mcp_servers[1].args.size(), 2u);
    EXPECT_FALSE(m.mcp_servers[1].enabled);
}

TEST(ExtensionManifestTest, ParseSkills)
{
    auto m = parseManifest(validManifest());

    ASSERT_EQ(m.skills.size(), 1u);
    EXPECT_EQ(m.skills[0].name, "code-review");
}

TEST(ExtensionManifestTest, ParseCommands)
{
    auto m = parseManifest(validManifest());

    ASSERT_EQ(m.commands.size(), 1u);
    EXPECT_EQ(m.commands[0].name, "review");
}

TEST(ExtensionManifestTest, ParseContextFiles)
{
    auto m = parseManifest(validManifest());

    ASSERT_EQ(m.context_files.size(), 1u);
    EXPECT_EQ(m.context_files[0].path, "docs/*.md");
}

TEST(ExtensionManifestTest, EmptyManifest)
{
    auto m = parseManifest("");

    EXPECT_FALSE(m.valid);
    EXPECT_FALSE(m.error.empty());
}

TEST(ExtensionManifestTest, MissingName)
{
    auto m = parseManifest(R"({"version": "1.0"})");

    EXPECT_FALSE(m.valid);
    EXPECT_FALSE(m.error.empty());
}

TEST(ExtensionManifestTest, InvalidJson)
{
    auto m = parseManifest("{bad json");

    EXPECT_FALSE(m.valid);
    EXPECT_FALSE(m.error.empty());
}

TEST(ExtensionManifestTest, MinimalValidManifest)
{
    auto m = parseManifest(R"({"name": "minimal"})");

    EXPECT_TRUE(m.valid);
    EXPECT_EQ(m.name, "minimal");
    EXPECT_EQ(m.display_name, "minimal");
    EXPECT_TRUE(m.mcp_servers.empty());
    EXPECT_TRUE(m.skills.empty());
    EXPECT_TRUE(m.commands.empty());
    EXPECT_TRUE(m.plugin_paths.empty());
}

TEST(ExtensionManifestTest, ParsePlugins)
{
    auto m = parseManifest(R"({
        "name": "with-plugins",
        "plugins": ["hooks/audit.so", "hooks/formatter.so"]
    })");

    EXPECT_TRUE(m.valid);
    ASSERT_EQ(m.plugin_paths.size(), 2u);
    EXPECT_EQ(m.plugin_paths[0], "hooks/audit.so");
    EXPECT_EQ(m.plugin_paths[1], "hooks/formatter.so");
}
