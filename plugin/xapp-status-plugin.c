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

#define SETTINGS_SCHEMA "org.x.apps.xfce4-status-plugin"
#define KEY_COLOR_ICON_SIZE "color-icon-size"
#define KEY_SYMBOLIC_ICON_SIZE "symbolic-icon-size"

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

  GSettings *settings;
};

/* define the plugin */
XFCE_PANEL_DEFINE_PLUGIN (XAppStatusPlugin, xapp_status_plugin)

static gboolean xapp_status_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                                                        gint             size);
static void     xapp_status_plugin_screen_position_changed (XfcePanelPlugin   *panel_plugin,
                                                                   XfceScreenPosition position);
static gint     get_color_icon_size (XAppStatusPlugin *plugin);
static gint     get_symbolic_icon_size (XAppStatusPlugin *plugin);


static void
xapp_status_plugin_init (XAppStatusPlugin *plugin)
{
  plugin->monitor = NULL;
  plugin->lookup_table = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
}

static gchar *
get_unique_key (GDBusProxy *proxy)
{
    const gchar *name = xapp_status_icon_interface_get_name (XAPP_STATUS_ICON_INTERFACE (proxy));
    const gchar *path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (proxy));

    return g_strconcat (name, path, NULL);
}

static gint
compare_icons (gpointer a,
               gpointer b)
{
    XAppStatusIconInterface *proxy_a, *proxy_b;
    gboolean sym_a, sym_b;
    gint res;

    proxy_a = status_icon_get_proxy (STATUS_ICON (a));
    proxy_b = status_icon_get_proxy (STATUS_ICON (b));

    sym_a = g_strstr_len (xapp_status_icon_interface_get_icon_name (proxy_a), -1, "symbolic") != NULL;
    sym_b = g_strstr_len (xapp_status_icon_interface_get_icon_name (proxy_b), -1, "symbolic") != NULL;

    if (sym_a && !sym_b)
    {
        return 1;
    }

    if (sym_b && !sym_a)
    {
        return -1;
    }

    res = g_utf8_collate (xapp_status_icon_interface_get_name (proxy_a),
                          xapp_status_icon_interface_get_name (proxy_b));

    if (res != 0)
    {
        return res;
    }

    gchar *key_a, *key_b;

    key_a = get_unique_key (G_DBUS_PROXY (proxy_a));
    key_b = get_unique_key (G_DBUS_PROXY (proxy_b));

    res = g_utf8_collate (key_a, key_b);

    g_free (key_a);
    g_free (key_b);

    return res;
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
    gchar *key;

    key = get_unique_key (G_DBUS_PROXY (proxy));
    icon = g_hash_table_lookup (plugin->lookup_table,
                                key);

    if (icon)
    {
        // Or should we remove the existing one and add this one??
        return;
    }

    icon = status_icon_new (proxy,
                            get_color_icon_size (plugin),
                            get_symbolic_icon_size (plugin));

    gtk_container_add (GTK_CONTAINER (plugin->icon_box),
                       GTK_WIDGET (icon));

    g_hash_table_insert (plugin->lookup_table,
                         g_strdup (key),
                         icon);

    g_free (key);

    g_signal_connect_swapped (icon, "re-sort", G_CALLBACK (sort_icons), plugin);
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
    gchar *key;

    key = get_unique_key (G_DBUS_PROXY (proxy));
    icon = g_hash_table_lookup (plugin->lookup_table,
                                key);

    if (!icon)
    {
        return;
    }

    gtk_container_remove (GTK_CONTAINER (plugin->icon_box),
                          GTK_WIDGET (icon));

    g_hash_table_remove (plugin->lookup_table,
                         key);

    g_free (key);

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
color_icon_size_changed (GtkWidget *item,
                         gpointer   user_data)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (user_data);
    gint size = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "height"));

    g_settings_set_int (plugin->settings, KEY_COLOR_ICON_SIZE, size);

    xapp_status_plugin_size_changed (XFCE_PANEL_PLUGIN (plugin),
                                     xfce_panel_plugin_get_size (XFCE_PANEL_PLUGIN (plugin)));
}

static void
symbolic_icon_size_changed (GtkWidget *item,
                            gpointer   user_data)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (user_data);
    gint size = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "height"));

    g_settings_set_int (plugin->settings, KEY_SYMBOLIC_ICON_SIZE, size);

    xapp_status_plugin_size_changed (XFCE_PANEL_PLUGIN (plugin),
                                     xfce_panel_plugin_get_size (XFCE_PANEL_PLUGIN (plugin)));
}

typedef struct
{
  const gchar *label;
  gint height;
} IconSizeDef;

static const IconSizeDef color_defs[] = {
    { N_("Use optimal size for panel"), -1 },
    { N_("16px"),                        16},
    { N_("22px"),                        22},
    { N_("24px"),                        24},
    { N_("32px"),                        32},
    { N_("48px"),                        48}
};

static const IconSizeDef symbolic_defs[] = {
    { N_("10px"),                        10},
    { N_("16px"),                        16},
    { N_("18px"),                        18},
    { N_("20px"),                        20},
    { N_("24px"),                        24},
    { N_("32px"),                        32},
    { N_("48px"),                        48}
};

static gint
get_color_icon_size (XAppStatusPlugin *plugin)
{
    gint size, panel_height;

    size = g_settings_get_int (plugin->settings, KEY_COLOR_ICON_SIZE);
    panel_height = xfce_panel_plugin_get_size (XFCE_PANEL_PLUGIN (plugin));

    if (size > 0 && size < panel_height)
    {
        return size;
    }

    if (panel_height < 22)
        return 16;
    else
    if (panel_height < 24)
        return 22;
    else
    if (panel_height < 32)
        return 24;
    else
    if (panel_height < 48)
        return 32;

    return 48;
}

static gint
get_symbolic_icon_size (XAppStatusPlugin *plugin)
{
    gint size, panel_height;

    size = g_settings_get_int (plugin->settings, KEY_SYMBOLIC_ICON_SIZE);
    panel_height = xfce_panel_plugin_get_size (XFCE_PANEL_PLUGIN (plugin));

    if (size > 0 && size < panel_height)
    {
        return size;
    }

    return panel_height - 4;
}

static void
add_menu_items (XAppStatusPlugin *plugin)
{
    GtkWidget *item;
    GtkWidget *submenu;
    GtkWidget *radio_submenu;
    gint i;
    gint settings_size;

    item = gtk_menu_item_new_with_label (_("About"));
    gtk_widget_show (item);

    g_signal_connect (item,
                      "activate",
                      G_CALLBACK (show_about_dialog),
                      plugin);

    xfce_panel_plugin_menu_insert_item (XFCE_PANEL_PLUGIN (plugin),
                                        GTK_MENU_ITEM (item));

    item = gtk_menu_item_new_with_label (_("Icon sizes"));
    gtk_widget_show (item);

    xfce_panel_plugin_menu_insert_item (XFCE_PANEL_PLUGIN (plugin),
                                        GTK_MENU_ITEM (item));

    submenu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (submenu));

    item = gtk_menu_item_new_with_label (_("Color"));
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), item);

    radio_submenu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (radio_submenu));

    settings_size = g_settings_get_int (plugin->settings, KEY_COLOR_ICON_SIZE);
    GSList *button_group = NULL;

    for (i = 0; i < G_N_ELEMENTS (color_defs); i++)
    {
        GtkWidget *radio_item;

        radio_item = gtk_radio_menu_item_new_with_label (button_group, color_defs[i].label);
        g_object_set_data (G_OBJECT (radio_item), "height", GINT_TO_POINTER (color_defs[i].height));

        gtk_menu_shell_append (GTK_MENU_SHELL (radio_submenu), radio_item);

        g_signal_connect (radio_item,
                          "activate",
                          G_CALLBACK (color_icon_size_changed),
                          plugin);

        button_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (radio_item));

        if (settings_size == color_defs[i].height)
        {
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (radio_item), TRUE);
        }
    }

    gtk_widget_show_all (radio_submenu);

    item = gtk_menu_item_new_with_label (_("Symbolic"));
    gtk_widget_show (item);
    gtk_menu_shell_append (GTK_MENU_SHELL (submenu), item);

    radio_submenu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (radio_submenu));

    settings_size = g_settings_get_int (plugin->settings, KEY_SYMBOLIC_ICON_SIZE);
    button_group = NULL;

    for (i = 0; i < G_N_ELEMENTS (symbolic_defs); i++)
    {
        GtkWidget *radio_item;

        radio_item = gtk_radio_menu_item_new_with_label (button_group, symbolic_defs[i].label);
        g_object_set_data (G_OBJECT (radio_item), "height", GINT_TO_POINTER (symbolic_defs[i].height));

        gtk_menu_shell_append (GTK_MENU_SHELL (radio_submenu), radio_item);

        g_signal_connect (radio_item,
                          "activate",
                          G_CALLBACK (symbolic_icon_size_changed),
                          plugin);

        button_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (radio_item));

        if (settings_size == symbolic_defs[i].height)
        {
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (radio_item), TRUE);
        }
    }

    gtk_widget_show_all (radio_submenu);
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

    plugin->settings = g_settings_new (SETTINGS_SCHEMA);
    add_menu_items (plugin);
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
    GtkOrientation orientation = xfce_panel_plugin_get_orientation (panel_plugin);
    gint max_size;

    max_size = size / xfce_panel_plugin_get_nrows (panel_plugin);

    g_hash_table_iter_init (&iter, applet->lookup_table);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        StatusIcon *icon = STATUS_ICON (value);

        gtk_widget_set_size_request (GTK_WIDGET (icon),
                                     orientation == GTK_ORIENTATION_HORIZONTAL ? -1 : max_size,
                                     orientation == GTK_ORIENTATION_VERTICAL   ? -1 : max_size);

        status_icon_set_size (icon,
                              get_color_icon_size (applet),
                              get_symbolic_icon_size (applet));
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
