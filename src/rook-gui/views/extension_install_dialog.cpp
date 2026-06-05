#include <glib/gi18n.h>
#include "extension_install_dialog.hpp"
#include <gtk/gtk.h>

using namespace peel;

namespace rook::gui {

Signal<ExtensionInstallDialog, void(void)> ExtensionInstallDialog::sig_done;

PEEL_CLASS_IMPL(ExtensionInstallDialog, "RookExtensionInstallDialog", Gtk::Window)

inline void ExtensionInstallDialog::Class::init()
{
    sig_done = Signal<ExtensionInstallDialog, void(void)>::create("done");
}

inline void ExtensionInstallDialog::init(Class *)
{
    set_title(_("Install Extension"));
    set_modal(true);
    set_default_size(420, 200);

    auto content = Gtk::Box::create(Gtk::Orientation::VERTICAL, 12);
    content->set_margin_start(16);
    content->set_margin_end(16);
    content->set_margin_top(16);
    content->set_margin_bottom(16);

    auto label = Gtk::Label::create(
        _("Enter the GitHub URL of the extension to install:"));
    label->set_xalign(0.0f);
    label->set_wrap(true);
    content->append(std::move(label));

    auto url = Gtk::Entry::create();
    url->set_placeholder_text("https://github.com/user/extension");
    m_url_entry = url;
    content->append(std::move(url));

    auto status = Gtk::Label::create("");
    status->set_xalign(0.0f);
    status->set_use_markup(true);
    status->set_wrap(true);
    m_status_label = status;
    content->append(std::move(status));

    auto buttons = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 8);
    buttons->set_halign(Gtk::Align::END);

    auto cancel = Gtk::Button::create_with_label(_("Cancel"));
    cancel->connect_clicked([this](Gtk::Button *) {
        m_accepted = false;
        close();
    });
    buttons->append(std::move(cancel));

    auto install = Gtk::Button::create_with_label(_("Install"));
    install->add_css_class("suggested-action");
    m_install_button = install;
    install->connect_clicked([this](Gtk::Button *) { onInstall(); });
    buttons->append(std::move(install));

    content->append(std::move(buttons));

    set_child(std::move(content).release_floating_ptr());
}

FloatPtr<ExtensionInstallDialog> ExtensionInstallDialog::create()
{
    return Object::create<ExtensionInstallDialog>();
}

void ExtensionInstallDialog::onInstall()
{
    auto text = std::string(m_url_entry->get_text());
    if (text.empty()) {
        m_status_label->set_markup(
            "<span foreground=\"#e01b24\">Please enter a URL.</span>");
        return;
    }

    m_url = text;
    m_accepted = true;
    sig_done.emit(this);
    close();
}

} // namespace rook::gui
