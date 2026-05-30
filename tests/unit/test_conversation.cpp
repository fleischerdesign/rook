#include <gtest/gtest.h>
#include "rook/domain/conversation.hpp"

using namespace rook::domain;

TEST(ConversationManagerTest, CreateConversation) {
    ConversationManager mgr;
    auto conv = mgr.create("Test Chat", "llama3.1");

    EXPECT_FALSE(conv.id.empty());
    EXPECT_EQ(conv.title, "Test Chat");
    EXPECT_EQ(conv.model, "llama3.1");
    EXPECT_TRUE(conv.messages.empty());

    auto list = mgr.list();
    EXPECT_EQ(list.size(), 1u);
}

TEST(ConversationManagerTest, AddMessage) {
    ConversationManager mgr;
    auto conv = mgr.create("Chat", "gpt-4o");

    ChatMessage msg;
    msg.role = "user";
    msg.content = "Hello";
    mgr.addMessage(conv.id, std::move(msg));

    auto updated = mgr.open(conv.id);
    ASSERT_EQ(updated.messages.size(), 1u);
    EXPECT_EQ(updated.messages[0].role, "user");
    EXPECT_EQ(updated.messages[0].content, "Hello");
}

TEST(ConversationManagerTest, BuildLlmMessages) {
    ConversationManager mgr;
    auto conv = mgr.create("Chat", "gpt-4o");

    ChatMessage msg1;
    msg1.role = "system";
    msg1.content = "You are a bot";
    mgr.addMessage(conv.id, std::move(msg1));

    ChatMessage msg2;
    msg2.role = "user";
    msg2.content = "Hi";
    mgr.addMessage(conv.id, std::move(msg2));

    auto messages = mgr.buildLlmMessages(conv.id);
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0].role, "system");
    EXPECT_EQ(messages[1].role, "user");
}

TEST(ConversationManagerTest, RemoveConversation) {
    ConversationManager mgr;
    auto conv = mgr.create("Chat", "llama3.1");
    EXPECT_EQ(mgr.list().size(), 1u);

    mgr.remove(conv.id);
    EXPECT_EQ(mgr.list().size(), 0u);
}

TEST(ConversationManagerTest, ActiveConversation) {
    ConversationManager mgr;
    auto conv = mgr.create("Chat", "gpt-4o");

    auto active = mgr.active();
    ASSERT_TRUE(active.has_value());
    EXPECT_EQ(active->id, conv.id);

    mgr.close(conv.id);
    auto none = mgr.active();
    EXPECT_FALSE(none.has_value());
}

TEST(ConversationManagerTest, TitleGeneration) {
    ConversationManager mgr;
    auto conv = mgr.create("Untitled", "llama3.1");

    ChatMessage msg;
    msg.role = "user";
    msg.content = "What is the weather today?";
    mgr.addMessage(conv.id, std::move(msg));

    auto updated = mgr.open(conv.id);
    EXPECT_EQ(updated.title, "What is the weather today?");
}

TEST(ConversationManagerTest, TokenEstimation) {
    ConversationManager mgr;
    auto conv = mgr.create("Chat", "gpt-4o");

    ChatMessage msg;
    msg.role = "user";
    msg.content = "Hello world!";
    mgr.addMessage(conv.id, std::move(msg));

    auto tokens = mgr.estimateTokens(conv.id);
    EXPECT_GT(tokens, 0);
}
