#pragma once

#include <string>
#include <string_view>
#include <functional>

namespace rook::ports {

class UserInputPort {
public:
    virtual ~UserInputPort() = default;

    virtual void onSubmit(
        std::function<void(std::string_view chat_id, std::string_view text)> callback
    ) = 0;

    virtual void onNewChat(
        std::function<void()> callback
    ) = 0;

    virtual void onSelectChat(
        std::function<void(std::string_view chat_id)> callback
    ) = 0;

    virtual void onDeleteChat(
        std::function<void(std::string_view chat_id)> callback
    ) = 0;

    virtual void onToggleMute(
        std::function<void(bool muted)> callback
    ) = 0;

    virtual void onOpenSettings(
        std::function<void()> callback
    ) = 0;
};

} // namespace rook::ports
