#include <stdlib.h>
#include <gtk/gtk.h>

#include "sn-watcher-interface.h"
#include "sn-item-interface.h"

#define XAPP_TYPE_SN_WATCHER xapp_sn_watcher_get_type ()
G_DECLARE_FINAL_TYPE (XAppSnWatcher, xapp_sn_watcher, XAPP, SN_WATCHER, GtkApplication)

struct _XAppSnWatcher
{
    GtkApplication parent_instance;

    SnWatcherInterface *skeleton;
    GDBusConnection *connection;

    guint owner_id;
    guint listener_id;

    GHashTable *items;
};

// typedef GtkApplicationClass XAppSnWatcherClass;

G_DEFINE_TYPE (XAppSnWatcher, xapp_sn_watcher, GTK_TYPE_APPLICATION)

#define NOTIFICATION_WATCHER_NAME "org.kde.StatusNotifierWatcher"
#define NOTIFICATION_WATCHER_PATH "/StatusNotifierWatcher"
#define STATUS_ICON_MONITOR_PREFIX "org.x.StatusIconMonitor"

#define FDO_DBUS_NAME "org.freedesktop.DBus"
#define FDO_DBUS_PATH "/org/freedesktop/DBus"

#define STATUS_ICON_MONITOR_MATCH "org.x.StatusIconMonitor"

static void watcher_shutdown (XAppSnWatcher *watcher);

static void
watcher_activate (GApplication *application)
{
  // new_window (application, NULL);
}

static void
watcher_open (GApplication  *application,
                GFile        **files,
                gint           n_files,
                const gchar   *hint)
{
  // gint i;

  // for (i = 0; i < n_files; i++)
  //   new_window (application, files[i]);
}

static void
watcher_finalize (GObject *object)
{
  G_OBJECT_CLASS (xapp_sn_watcher_parent_class)->finalize (object);
}


static void
name_owner_changed (GDBusConnection *connection,
                    const gchar     *sender_name,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
    // XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);
    g_debug("XAppSnWatcher: NameOwnerChanged signal received");

    // refresh_icon (self);
}

static void
add_name_listener (XAppSnWatcher *watcher)
{
    g_debug ("XAppSnWatcher: Adding NameOwnerChanged listener for status monitor existence");

    watcher->listener_id = g_dbus_connection_signal_subscribe (watcher->connection,
                                                               FDO_DBUS_NAME,
                                                               FDO_DBUS_NAME,
                                                               "NameOwnerChanged",
                                                               FDO_DBUS_PATH,
                                                               STATUS_ICON_MONITOR_MATCH,
                                                               G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE,
                                                               name_owner_changed,
                                                               watcher,
                                                               NULL);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
    XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    g_warning ("XAppSnWatcher: Lost name, exiting.");

    watcher_shutdown (watcher);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
    // XAppSnWatcher *watcher = XAPP_SN_WATCHER (user_data);

    g_debug ("XAppSnWatcher: name acquired on dbus.");
}

static gboolean
handle_register_host (SnWatcherInterface     *skeleton,
                      GDBusMethodInvocation  *invocation,
                      const gchar*            service,
                      XAppSnWatcher          *watcher)
{
    // Nothing to do - we wouldn't be here if there wasn't a host (status applet)
    sn_watcher_interface_complete_register_status_notifier_host (skeleton,
                                                                 invocation);

    return TRUE;
}

static void
update_published_items (XAppSnWatcher *watcher)
{
}

static gboolean
create_key (const gchar  *sender,
            const gchar  *service,
            gchar       **key,
            gchar       **bus_name,
            gchar       **path)
{
    gchar *temp_key, *temp_bname, *temp_path;

    temp_key = temp_bname = temp_path = NULL;
    *key = *bus_name = *path = NULL;

    if (g_str_has_prefix (service, "/"))
    {
        temp_bname = g_strdup (sender);
        temp_path = g_strdup (service);
    }
    else
    {
        temp_bname = g_strdup (service);
        temp_path = g_strdup ("/StatusNotifierItem");
    }

    if (!g_dbus_is_name (temp_bname))
    {
        g_free (temp_bname);
        g_free (temp_path);

        return FALSE;
    }

    temp_key = g_strdup_printf ("%s%s", temp_bname, temp_path);

    g_debug ("Key: '%s'  -  from busname '%s', path '%s'", temp_key, temp_bname, temp_path);

    *key = temp_key;
    *bus_name = temp_bname;
    *path = temp_path;

    return TRUE;
}

static gboolean
handle_register_item (SnWatcherInterface     *skeleton,
                      GDBusMethodInvocation  *invocation,
                      const gchar*            service,
                      XAppSnWatcher          *watcher)
{
    SnItemWrapper *wrapper;

    SnItemInterfaceProxy *proxy;
    GError *error;
    const gchar *sender;
    g_autofree gchar *key, *bus_name, *path;

    sender = g_dbus_method_invocation_get_sender (invocation);

    if (!create_key (sender, service, &key, &bus_name, &path))
    {
        error = g_error_new (g_dbus_error_quark (),
                             G_DBUS_ERROR_INVALID_ARGS,
                             "Invalid bus name from: %s, %s", service, sender);
        g_dbus_method_invocation_return_gerror (invocation, error);

        return FALSE;
    }

    proxy = g_hash_table_lookup (watcher->items, key);

    if (proxy == NULL)
    {
        error = NULL;

        proxy = sn_item_interface_proxy_new_sync (watcher->connection,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  bus_name,
                                                  path,
                                                  NULL,
                                                  &error);

        if (error != NULL)
        {
            g_warning ("Could not create new status notifier proxy item for item at %s: %s", bus_name, error->message);

            g_dbus_method_invocation_return_gerror (invocation, error);

            return FALSE;
        }

        wrapper = sn_item_wrapper_new (proxy);

        g_hash_table_insert (watcher->items,
                             key,
                             wrapper);

        update_published_items (watcher);
    }

    sn_watcher_interface_complete_register_status_notifier_item (invocation);
    sn_watcher_interface_emit_status_notifier_item_registered (service);

    return TRUE;
}

static gboolean
export_watcher_interface (XAppSnWatcher *watcher)
{
    GError *error = NULL;

    if (watcher->skeleton) {
        return TRUE;
    }

    watcher->skeleton = sn_watcher_interface_skeleton_new ();

    g_debug ("XAppSnWatcher: exporting StatusNotifierWatcher dbus interface to %s", NOTIFICATION_WATCHER_PATH);

    g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (watcher->skeleton),
                                      watcher->connection,
                                      NOTIFICATION_WATCHER_PATH,
                                      &error);

    if (error != NULL) {
        g_critical ("XAppSnWatcher: could not export StatusNotifierWatcher interface: %s", error->message);
        g_error_free (error);

        return FALSE;
    }

    g_signal_connect (watcher->skeleton,
                      "handle-register-status-notifier-item",
                      G_CALLBACK (handle_register_item),
                      watcher);

    g_signal_connect (watcher->skeleton,
                      "handle-register-status-notifier-host",
                      G_CALLBACK (handle_register_item),
                      watcher);

    return TRUE;
}

static void
continue_startup (XAppSnWatcher *watcher)
{

    g_debug ("XAppSnWatcher: Trying to acquire session bus connection");

    watcher->connection = g_bus_get_sync (G_BUS_TYPE_SESSION,
                                          NULL,
                                          on_session_bus_connected,
                                          watcher);

    export_watcher_interface (watcher);

    watcher->owner_id = g_dbus_own_name_on_connection (watcher->connection,
                                                       NOTIFICATION_WATCHER_NAME,
                                                       G_DBUS_CONNECTION_FLAGS_REPLACE,
                                                       on_name_acquired,
                                                       on_name_lost,
                                                       watcher,
                                                       NULL);
}

static void
watcher_startup (GApplication *application)
{
    XAppSnWatcher *watcher = (XAppSnWatcher*) application;
    GtkApplication *app = GTK_APPLICATION (application);

    G_APPLICATION_CLASS (xapp_sn_watcher_parent_class)->startup (application);

    watcher->items = g_hash_table_new (g_str_hash, g_str_equal);

    add_name_listener (watcher);

    if (!xapp_status_icon_any_monitors ())
    {
        continue_startup (watcher);
    }
    else
    {
        print("No active monitors, exiting in 30s")
    }
}

static gint
watcher_command_line (GApplication *application)
{
 
    // def do_command_line(self, command_line):
        // options = command_line.get_options_dict()
        // options = options.end().unpack()

        // if "quit" in options:
        //     if self.watcher != None:
        //         print("Shutting down the XApp StatusNotifierWatcher")
        //         self.shutdown()
        //     else:
        //         print("XApp StatusNotifierWatcher not running")
        //         exit(0)

    return 0
}


static void
watcher_shutdown (GApplication *application)
{
    XAppSnWatcher *watcher = (XAppSnWatcher *) application;

  // if (watcher->timeout)
  //   {
  //     g_source_remove (watcher->timeout);
  //     watcher->timeout = 0;
  //   }

  G_APPLICATION_CLASS (xapp_sn_watcher_parent_class)->shutdown (application);
}

static void
watcher_init (XAppSnWatcher *app)
{
}

static void
watcher_class_init (XAppSnWatcherClass *class)
{
  GApplicationClass *application_class = G_APPLICATION_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  application_class->startup = watcher_startup;
  // application_class->shutdown = watcher_shutdown;
  application_class->activate = watcher_activate;
  application_class->open = watcher_open;
  application_class->command_line = watcher_command_line;

  object_class->finalize = watcher_finalize;
}

XAppSnWatcher *
watcher_new (void)
{
  XAppSnWatcher *watcher;

  g_set_application_name ("xapp-sn-watcher");

  watcher = g_object_new (watcher_get_type (),
                          "application-id", "org.x.StatusNotifierWatcher",
                          "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                          "inactivity-timeout", 30000,
                          "register-session", TRUE,
                          NULL);

  return watcher;
}

int
main (int argc, char **argv)
{
  XAppSnWatcher *watcher;
  int status;

  watcher = watcher_new ();

  status = g_application_run (G_APPLICATION (watcher), argc, argv);

  g_object_unref (watcher);

  return status;
}
