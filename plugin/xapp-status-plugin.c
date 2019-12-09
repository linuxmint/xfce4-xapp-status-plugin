/*
 * Copyright (c) 2005-2007 Jasper Huijsmans <jasper@xfce.org>
 * Copyright (c) 2007-2010 Nick Schermer <nick@xfce.org>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <libxapp/xapp-statusicon-interface.h>
#include <libxapp/xapp-status-icon-monitor.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include "xapp-status-plugin.h"
#include "status-icon.h"

struct _XAppStatusPluginClass
{
  XfcePanelPluginClass __parent__;
};

struct _XAppStatusPlugin
{
  XfcePanelPlugin __parent__;

  /* dbus monitor */
  XAppStatusIconMonitor *monitor;

  /* A quick reference to our list box items */
  GHashTable *lookup_table;

  /* A GtkListBox to hold our icons */
  GtkWidget *icon_box;
};

/* define the plugin */
XFCE_PANEL_DEFINE_PLUGIN (XAppStatusPlugin, xapp_status_plugin)

static gboolean xapp_status_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                                                        gint             size);
static void     xapp_status_plugin_screen_position_changed (XfcePanelPlugin   *panel_plugin,
                                                                   XfceScreenPosition position);

static void
xapp_status_plugin_init (XAppStatusPlugin *plugin)
{
  plugin->monitor = NULL;
  plugin->lookup_table = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
}

static gint
compare_icons (gpointer a,
               gpointer b)
{
    XAppStatusIconInterface *proxy_a, *proxy_b;
    gboolean sym_a, sym_b;

    proxy_a = status_icon_get_proxy (STATUS_ICON (a));
    proxy_b = status_icon_get_proxy (STATUS_ICON (b));

    sym_a = g_strstr_len (xapp_status_icon_interface_get_icon_name (proxy_a), -1, "symbolic") != NULL;
    sym_b = g_strstr_len (xapp_status_icon_interface_get_icon_name (proxy_b), -1, "symbolic") != NULL;

    if (sym_a && !sym_b) {
        return 1;
    }

    if (sym_b && !sym_a) {
        return -1;
    }

    return g_utf8_collate (xapp_status_icon_interface_get_name (proxy_a),
                           xapp_status_icon_interface_get_name (proxy_b));
}

static void
sort_icons (XAppStatusPlugin *plugin)
{
    GList *unordered_icons, *icons, *iter;

    unordered_icons = g_hash_table_get_values (plugin->lookup_table);

    if (!unordered_icons)
    {
        return;
    }

    icons = g_list_copy (unordered_icons);
    icons = g_list_sort (icons, (GCompareFunc) compare_icons);
    icons = g_list_reverse (icons);

    iter = icons;

    while (iter)
    {
        StatusIcon *icon = STATUS_ICON (iter->data);

        gtk_box_reorder_child (GTK_BOX (plugin->icon_box),
                               GTK_WIDGET (icon),
                               0);

        iter = iter->next;
    }

    g_list_free (icons);
}

static void
on_icon_added (XAppStatusIconMonitor        *monitor,
               XAppStatusIconInterface      *proxy,
               gpointer                      user_data)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (user_data);
    XfcePanelPlugin *panel_plugin = XFCE_PANEL_PLUGIN (plugin);
    StatusIcon *icon;
    const gchar *name;

    name = xapp_status_icon_interface_get_name (XAPP_STATUS_ICON_INTERFACE (proxy));

    icon = g_hash_table_lookup (plugin->lookup_table,
                                name);

    if (icon)
    {
        // Or should we remove the existing one and add this one??
        return;
    }

    icon = status_icon_new (proxy);

    gtk_container_add (GTK_CONTAINER (plugin->icon_box),
                       GTK_WIDGET (icon));

    g_hash_table_insert (plugin->lookup_table,
                         g_strdup (name),
                         icon);

    sort_icons (plugin);

    xapp_status_plugin_size_changed (panel_plugin,
                                            xfce_panel_plugin_get_size (panel_plugin));

    xapp_status_plugin_screen_position_changed (panel_plugin,
                                                       xfce_panel_plugin_get_screen_position (panel_plugin));
}

static void
on_icon_removed (XAppStatusIconMonitor        *monitor,
                 XAppStatusIconInterface      *proxy,
                 gpointer                      user_data)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (user_data);
    XfcePanelPlugin *panel_plugin = XFCE_PANEL_PLUGIN (plugin);
    StatusIcon *icon;
    const gchar *name;

    name = xapp_status_icon_interface_get_name (XAPP_STATUS_ICON_INTERFACE (proxy));
    icon = g_hash_table_lookup (plugin->lookup_table,
                                name);

    if (!icon)
    {
        return;
    }

    gtk_container_remove (GTK_CONTAINER (plugin->icon_box),
                          GTK_WIDGET (icon));

    g_hash_table_remove (plugin->lookup_table,
                         name);

    sort_icons (plugin);

    xapp_status_plugin_size_changed (XFCE_PANEL_PLUGIN (plugin),
                                            xfce_panel_plugin_get_size (panel_plugin));

    xapp_status_plugin_screen_position_changed (XFCE_PANEL_PLUGIN (plugin),
                                                       xfce_panel_plugin_get_screen_position (panel_plugin));
}

static void
show_about_dialog (GtkMenuItem *item,
                   gpointer     user_data)
{
    GtkAboutDialog *dialog;

    dialog = GTK_ABOUT_DIALOG (gtk_about_dialog_new ());

    gtk_about_dialog_set_program_name (dialog, _("XApp Status Plugin"));
    gtk_about_dialog_set_version (dialog, VERSION);
    gtk_about_dialog_set_license_type (dialog, GTK_LICENSE_GPL_3_0);
    gtk_about_dialog_set_website (dialog, "https://www.github.com/linuxmint/xfce4-xapp-status-plugin");
    gtk_about_dialog_set_logo_icon_name (dialog, "panel-applets");
    gtk_about_dialog_set_comments (dialog, _("Area where XApp Status icons appear"));

    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
add_about_menu_item (XAppStatusPlugin *plugin)
{
    GtkMenuItem *item;

    item = GTK_MENU_ITEM (gtk_menu_item_new_with_label (_("About")));
    gtk_widget_show (GTK_WIDGET (item));

    g_signal_connect (item,
                      "activate",
                      G_CALLBACK (show_about_dialog),
                      plugin);

    xfce_panel_plugin_menu_insert_item (XFCE_PANEL_PLUGIN (plugin),
                                        item);
}


static void
xapp_status_plugin_construct (XfcePanelPlugin *panel_plugin)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);

    plugin->monitor = xapp_status_icon_monitor_new ();

    g_signal_connect (plugin->monitor,
                      "icon-added",
                      G_CALLBACK (on_icon_added),
                      plugin);


    g_signal_connect (plugin->monitor,
                      "icon-removed",
                      G_CALLBACK (on_icon_removed),
                      plugin);

    plugin->icon_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,
                                    0);

    gtk_widget_show (plugin->icon_box);
    gtk_container_set_border_width (GTK_CONTAINER (plugin->icon_box),
                                    INDICATOR_BOX_BORDER);

    gtk_container_add (GTK_CONTAINER (plugin),
                       plugin->icon_box);

    gtk_widget_show_all (GTK_WIDGET (plugin));

    xfce_panel_plugin_set_small (panel_plugin, TRUE);

    xapp_status_plugin_screen_position_changed (panel_plugin,
                                                       xfce_panel_plugin_get_orientation (panel_plugin));

    add_about_menu_item (plugin);
}

/* This is our dispose */
static void
xapp_status_plugin_free_data (XfcePanelPlugin *panel_plugin)
{
  XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);

  g_hash_table_destroy (plugin->lookup_table);

  if (G_LIKELY (plugin->monitor != NULL))
    {
      g_clear_object (&plugin->monitor);
    }
}

static void
xapp_status_plugin_screen_position_changed (XfcePanelPlugin   *panel_plugin,
                                                   XfceScreenPosition position)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);
    GtkPositionType xapp_orientation = GTK_POS_TOP;
    GtkOrientation widget_orientation = GTK_ORIENTATION_HORIZONTAL;
    GHashTableIter iter;
    gpointer key, value;

    if (xfce_screen_position_is_top (position))
    {
        xapp_orientation = GTK_POS_TOP;
    }
    else
    if (xfce_screen_position_is_bottom (position))
    {
        xapp_orientation = GTK_POS_BOTTOM;
    }
    else
    if (xfce_screen_position_is_left (position))
    {
        xapp_orientation = GTK_POS_LEFT;
        widget_orientation = GTK_ORIENTATION_VERTICAL;
    }
    else
    if (xfce_screen_position_is_right (position))
    {
        xapp_orientation = GTK_POS_RIGHT;
        widget_orientation = GTK_ORIENTATION_VERTICAL;
    }

    g_hash_table_iter_init (&iter, plugin->lookup_table);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        StatusIcon *icon = STATUS_ICON (value);

        status_icon_set_orientation (icon, xapp_orientation);
    }

    gtk_orientable_set_orientation (GTK_ORIENTABLE (plugin->icon_box),
                                    widget_orientation);
}

static gboolean
xapp_status_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                                        gint             size)
{
    XAppStatusPlugin *applet = XAPP_STATUS_PLUGIN (panel_plugin);

    GHashTableIter iter;
    gpointer key, value;
    gint max_size;

    max_size = size / xfce_panel_plugin_get_nrows (panel_plugin);

    g_hash_table_iter_init (&iter, applet->lookup_table);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        StatusIcon *icon = STATUS_ICON (value);

        gtk_widget_set_size_request (GTK_WIDGET (icon),
                                     max_size,
                                     max_size);

        status_icon_set_size (icon,
                              xfce_panel_plugin_get_icon_size (panel_plugin));
    }

    gtk_widget_queue_resize (GTK_WIDGET (panel_plugin));

    return TRUE;
}

static void
xapp_status_plugin_class_init (XAppStatusPluginClass *klass)
{
  XfcePanelPluginClass *plugin_class;

  plugin_class = XFCE_PANEL_PLUGIN_CLASS (klass);
  plugin_class->construct = xapp_status_plugin_construct;
  plugin_class->free_data = xapp_status_plugin_free_data;
  plugin_class->size_changed = xapp_status_plugin_size_changed;
  plugin_class->screen_position_changed = xapp_status_plugin_screen_position_changed;

    /* Initialize gettext support */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
}
