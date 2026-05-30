#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace rook::ports {

struct ListItem {
    std::string id;
    std::string title;
    std::string subtitle;
};

enum class NotificationLevel {
    Info,
    Warning,
    Error,
};

class UserOutputPort {
public:
    virtual ~UserOutputPort() = default;

    virtual void showMessage(
        std::string_view chat_id,
        std::string_view role,
        std::string_view content
    ) = 0;

    virtual void streamChunk(
        std::string_view chat_id,
        std::string_view content,
        bool is_final
    ) = 0;

    virtual void showChatList(const std::vector<ListItem>& chats) = 0;

    virtual void setAudioState(int state) = 0;

    virtual void showNotification(
        std::string_view title,
        std::string_view body,
        NotificationLevel level
    ) = 0;

    virtual void showPermissionPrompt(
        std::string_view prompt_id,
        std::string_view tool_name,
        std::string_view arguments,
        std::function<void(bool granted, bool always)> on_response
    ) = 0;

    virtual void setProcessingIndicator(bool active) = 0;
};

} // namespace rook::ports
