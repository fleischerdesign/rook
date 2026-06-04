#pragma once

#include "rook/ports/tool_port.hpp"
#include "rook/domain/conversation.hpp"

namespace rook::core { class DomainActor; }

namespace rook::gui {

class GtkPermissionPort final : public rook::ports::ToolPermissionPort {
public:
    explicit GtkPermissionPort(rook::core::DomainActor& actor)
        : m_actor(actor) {}

    bool isWhitelisted(std::string_view chat_id,
                        std::string_view tool_name) const override {
        return m_actor.conv().isToolWhitelisted(chat_id, tool_name);
    }

    void addToWhitelist(std::string_view chat_id,
                         std::string_view tool_name) override {
        m_actor.conv().addWhitelistedTool(chat_id, tool_name);
    }

private:
    rook::core::DomainActor& m_actor;
};

} // namespace rook::gui
