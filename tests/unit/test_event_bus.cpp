#include <gtest/gtest.h>
#include "rook/domain/event_bus.hpp"
#include "rook/domain/events.hpp"

using namespace rook::domain;

TEST(EventBusTest, SubscribeAndPublish) {
    EventBus bus;
    int received = 0;

    auto id = bus.subscribe<LlmCompleted>([&](const LlmCompleted&) {
        received++;
    });

    bus.publish(LlmCompleted{"chat-1", 42});
    EXPECT_EQ(received, 1);

    bus.publish(LlmCompleted{"chat-2", 10});
    EXPECT_EQ(received, 2);

    bus.unsubscribe(id);
    bus.publish(LlmCompleted{"chat-3", 0});
    EXPECT_EQ(received, 2);
}

TEST(EventBusTest, MultipleSubscribers) {
    EventBus bus;
    int a = 0, b = 0;

    bus.subscribe<UserInputReceived>([&](const UserInputReceived&) { a++; });
    bus.subscribe<UserInputReceived>([&](const UserInputReceived&) { b++; });

    bus.publish(UserInputReceived{"chat", "hello", "text", ""});

    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(EventBusTest, DifferentEventTypesDoNotInterfere) {
    EventBus bus;
    int chats = 0;
    int deleted = 0;

    bus.subscribe<ChatCreated>([&](const ChatCreated&) { chats++; });
    bus.subscribe<ChatSelected>([&](const ChatSelected&) { deleted++; });

    bus.publish(ChatCreated{"id-1"});
    EXPECT_EQ(chats, 1);
    EXPECT_EQ(deleted, 0);

    bus.publish(ChatSelected{"id-2"});
    EXPECT_EQ(chats, 1);
    EXPECT_EQ(deleted, 1);
}

TEST(EventBusTest, EventPayloadIsCorrect) {
    EventBus bus;
    std::string last_id;
    std::string last_content;

    bus.subscribe<UserInputReceived>([&](const UserInputReceived& e) {
        last_id = e.chat_id;
        last_content = e.content;
    });

    bus.publish(UserInputReceived{"chat-42", "hello world", "text", ""});

    EXPECT_EQ(last_id, "chat-42");
    EXPECT_EQ(last_content, "hello world");
}
