#pragma once
#include <peel/Gtk/Gtk.h>
#include <peel/Adw/Adw.h>
#include <peel/class.h>
#include <functional>
#include <memory>
#include "rook/ports/llm_port.hpp"
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/security/security_manager.hpp"
#include "rook/ports/extension_port.hpp"
#include "rook/adapters/extension/extension_manifest.hpp"

namespace rook::gui {

class PreferencesWindow final : public peel::Adw::PreferencesDialog {
    PEEL_SIMPLE_CLASS(PreferencesWindow, peel::Adw::PreferencesDialog)
    inline void init(Class*);
public:
    static peel::FloatPtr<PreferencesWindow> create(rook::ports::LlmPort &llm,
                                                       rook::adapters::mcp::McpServerManager *mcp,
                                                       rook::adapters::security::SecurityManager *security,
                                                       rook::ports::ExtensionPort *extensions,
                                                       std::vector<rook::adapters::extension::CustomSkill> *custom_skills,
                                                       std::function<void()> on_changed);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rook::gui
