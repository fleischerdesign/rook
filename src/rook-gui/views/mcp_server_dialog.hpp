#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <string>
#include <string_view>
#include <vector>
#include "rook/adapters/mcp/mcp_server_manager.hpp"
#include "rook/adapters/security/capability.hpp"

namespace rook::gui {

class McpServerDialog : public peel::Gtk::Window
{
    PEEL_SIMPLE_CLASS(McpServerDialog, peel::Gtk::Window)

    static peel::Signal<McpServerDialog, void(void)> sig_done;

    peel::Gtk::ToggleButton *m_stdio_toggle = nullptr;
    peel::Gtk::ToggleButton *m_url_toggle = nullptr;
    peel::Gtk::Stack *m_transport_stack = nullptr;
    peel::Gtk::Entry *m_id_entry = nullptr;
    peel::Gtk::Entry *m_command_entry = nullptr;
    peel::Gtk::Box *m_args_list = nullptr;
    peel::Gtk::Entry *m_url_entry = nullptr;
    peel::Gtk::Box *m_headers_list = nullptr;
    peel::Gtk::Expander *m_cap_expander = nullptr;
    peel::Gtk::Box *m_read_paths_list = nullptr;
    peel::Gtk::Box *m_write_paths_list = nullptr;
    peel::Gtk::Switch *m_network_switch = nullptr;
    peel::Gtk::SpinButton *m_memory_spin = nullptr;
    peel::Gtk::SpinButton *m_cpu_spin = nullptr;
    peel::Gtk::Box *m_memory_row = nullptr;
    peel::Gtk::Box *m_cpu_row = nullptr;
    peel::Gtk::Switch *m_enabled_switch = nullptr;
    peel::Gtk::Button *m_test_button = nullptr;
    peel::Gtk::Label *m_test_result = nullptr;

    std::vector<peel::Gtk::Entry*> m_arg_entries;
    std::vector<peel::Gtk::Entry*> m_read_path_entries;
    std::vector<peel::Gtk::Entry*> m_write_path_entries;

    struct HeaderRow {
        peel::Gtk::Entry* key_entry = nullptr;
        peel::Gtk::Entry* value_entry = nullptr;
        peel::Gtk::Box* row = nullptr;
    };
    std::vector<HeaderRow> m_header_rows;

    bool m_accepted = false;
    bool m_edit_mode = false;
    std::string m_edit_id;

    inline void init(Class *);

    void onTransportChanged();
    void onSave();
    void onCancel();
    void onTestConnection();
    bool validateInput();
    void addArgEntry(std::string_view value = "");
    void addReadPathEntry(std::string_view value = "");
    void addWritePathEntry(std::string_view value = "");
    void addHeaderRow(std::string_view key = "", std::string_view value = "");
    std::vector<std::string> collectArgs() const;
    std::vector<std::string> collectReadPaths() const;
    std::vector<std::string> collectWritePaths() const;
    void setTestResult(bool ok, std::string_view message);
    void addPathEntry(peel::Gtk::Box *list,
                      std::vector<peel::Gtk::Entry*>& entries,
                      std::string_view value = "");
    void addArgEntryTo(peel::Gtk::Box *list,
                       std::vector<peel::Gtk::Entry*>& entries,
                       std::string_view value = "");

public:
    PEEL_SIGNAL_CONNECT_METHOD(done, sig_done)

    static peel::FloatPtr<McpServerDialog> create(
        const rook::adapters::mcp::McpServerConfig *existing = nullptr);

    bool wasAccepted() const { return m_accepted; }
    rook::adapters::mcp::McpServerConfig getConfig() const;
    rook::adapters::security::Capability getCapability() const;

    void setPresetIndex(guint idx);
    void addReadPath(std::string_view value);
    void addWritePath(std::string_view value);
    void setNetworkAllowed(bool allowed);
    void setMemoryMb(int64_t mb);
    void setCpuSecs(int64_t secs);
    void setEnabled(bool e);
};

} // namespace rook::gui
