#include "tray_icon.hpp"

#include <gio/gio.h>
#include <glib.h>
#include <spdlog/spdlog.h>

namespace rook::gui {

static const char kSniXml[] = R"(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="i" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <method name="Activate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="ContextMenu">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Scroll">
      <arg name="delta" type="i" direction="in"/>
      <arg name="orientation" type="s" direction="in"/>
    </method>
  </interface>
</node>
)";

struct TrayIcon::Impl {
    TrayIcon *owner = nullptr;
    std::string service_name;
    std::string title;
    std::string icon_name;
    guint owner_id = 0;
    guint obj_id = 0;
    bool active = false;

    void onBusAcquired(GDBusConnection *conn);
    void onNameAcquired(GDBusConnection *conn);
    void onNameLost(GDBusConnection *conn);
    void onMethodCall(GDBusConnection *conn, const char *sender,
                      const char *obj_path, const char *iface,
                      const char *method, GVariant *params,
                      GDBusMethodInvocation *invocation);
    GVariant *onGetProperty(GDBusConnection *conn, const char *sender,
                            const char *obj_path, const char *iface,
                            const char *property);
};

static GDBusInterfaceVTable kSniVTable = {
    // method_call
    [](GDBusConnection *conn, const char *sender, const char *obj_path,
       const char *iface, const char *method, GVariant *params,
       GDBusMethodInvocation *invocation, gpointer data) {
        auto *impl = static_cast<TrayIcon::Impl*>(data);
        impl->onMethodCall(conn, sender, obj_path, iface, method,
                           params, invocation);
    },
    // get_property
    [](GDBusConnection *conn, const char *sender, const char *obj_path,
       const char *iface, const char *property, GError ** /*error*/,
       gpointer data) -> GVariant* {
        auto *impl = static_cast<TrayIcon::Impl*>(data);
        return impl->onGetProperty(conn, sender, obj_path, iface, property);
    },
    nullptr, // set_property
    {0}
};

void TrayIcon::Impl::onBusAcquired(GDBusConnection *conn)
{
    spdlog::info("TrayIcon: bus acquired for {}", service_name);

    GError *error = nullptr;
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(kSniXml, &error);
    if (!info) {
        spdlog::error("TrayIcon: failed to parse XML: {}",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
        return;
    }

    auto *sni_iface = info->interfaces[0];
    obj_id = g_dbus_connection_register_object(
        conn, "/StatusNotifierItem", sni_iface,
        &kSniVTable, this, nullptr, &error);

    if (!obj_id) {
        spdlog::error("TrayIcon: failed to register object: {}",
                      error ? error->message : "unknown");
        if (error) g_error_free(error);
    }

    g_dbus_node_info_unref(info);
}

void TrayIcon::Impl::onNameAcquired(GDBusConnection *conn)
{
    spdlog::info("TrayIcon: name acquired for {}", service_name);
    active = true;

    auto *watcher = g_dbus_proxy_new_sync(
        conn, G_DBUS_PROXY_FLAGS_NONE, nullptr,
        "org.kde.StatusNotifierWatcher",
        "/StatusNotifierWatcher",
        "org.kde.StatusNotifierWatcher",
        nullptr, nullptr);

    if (watcher) {
        g_dbus_proxy_call(
            watcher, "RegisterStatusNotifierItem",
            g_variant_new("(s)", service_name.c_str()),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
            [](GObject*, GAsyncResult *res, gpointer) {
                GError *err = nullptr;
                auto *reply =
                    g_dbus_proxy_call_finish(G_DBUS_PROXY(res), res, &err);
                if (err) {
                    spdlog::warn("TrayIcon: failed to register with watcher: {}",
                                 err->message);
                    g_error_free(err);
                }
                if (reply) g_variant_unref(reply);
            }, nullptr);
        g_object_unref(watcher);
    }
}

void TrayIcon::Impl::onNameLost(GDBusConnection *)
{
    spdlog::warn("TrayIcon: name lost for {}", service_name);
    active = false;
}

void TrayIcon::Impl::onMethodCall(GDBusConnection * /*conn*/, const char * /*sender*/,
                                   const char * /*obj_path*/,
                                   const char * /*iface*/, const char *method,
                                   GVariant * /*params*/,
                                   GDBusMethodInvocation *invocation)
{
    if (g_strcmp0(method, "Activate") == 0) {
        if (owner->m_on_activate) owner->m_on_activate();
        g_dbus_method_invocation_return_value(invocation, nullptr);
    } else if (g_strcmp0(method, "SecondaryActivate") == 0) {
        if (owner->m_on_secondary) owner->m_on_secondary();
        g_dbus_method_invocation_return_value(invocation, nullptr);
    } else if (g_strcmp0(method, "ContextMenu") == 0) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
    } else if (g_strcmp0(method, "Scroll") == 0) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
    } else {
        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method %s", method);
    }
}

GVariant *TrayIcon::Impl::onGetProperty(GDBusConnection * /*conn*/,
                                          const char * /*sender*/,
                                          const char * /*obj_path*/,
                                          const char * /*iface*/,
                                          const char *property)
{
    if (g_strcmp0(property, "Category") == 0)
        return g_variant_new_string("ApplicationStatus");
    if (g_strcmp0(property, "Id") == 0)
        return g_variant_new_string(service_name.c_str());
    if (g_strcmp0(property, "Title") == 0)
        return g_variant_new_string(title.c_str());
    if (g_strcmp0(property, "Status") == 0)
        return g_variant_new_string(active ? "Active" : "Passive");
    if (g_strcmp0(property, "WindowId") == 0)
        return g_variant_new_int32(0);
    if (g_strcmp0(property, "IconName") == 0)
        return g_variant_new_string(icon_name.c_str());
    if (g_strcmp0(property, "ItemIsMenu") == 0)
        return g_variant_new_boolean(FALSE);
    return nullptr;
}

TrayIcon::TrayIcon(std::string service_name, std::string title,
                   std::string icon_name)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->owner = this;
    m_impl->service_name = std::move(service_name);
    m_impl->title = std::move(title);
    m_impl->icon_name = std::move(icon_name);

    m_impl->owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        m_impl->service_name.c_str(),
        G_BUS_NAME_OWNER_FLAGS_NONE,
        [](GDBusConnection *conn, const char * /*name*/, gpointer data) {
            static_cast<Impl*>(data)->onBusAcquired(conn);
        },
        [](GDBusConnection *conn, const char * /*name*/, gpointer data) {
            static_cast<Impl*>(data)->onNameAcquired(conn);
        },
        [](GDBusConnection *conn, const char * /*name*/, gpointer data) {
            static_cast<Impl*>(data)->onNameLost(conn);
        },
        m_impl.get(), nullptr);
}

TrayIcon::~TrayIcon()
{
    if (m_impl->obj_id) {
        GDBusConnection *conn =
            g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        if (conn) {
            g_dbus_connection_unregister_object(conn, m_impl->obj_id);
            g_object_unref(conn);
        }
    }
    if (m_impl->owner_id)
        g_bus_unown_name(m_impl->owner_id);
}

void TrayIcon::show()
{
    if (m_impl->active) return;
    m_impl->active = true;
    spdlog::info("TrayIcon: show");
}

void TrayIcon::hide()
{
    if (!m_impl->active) return;
    m_impl->active = false;
    spdlog::info("TrayIcon: hide");
}

} // namespace rook::gui
