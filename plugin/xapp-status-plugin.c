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

  /* A GtkGrid to hold our icons */
  GtkWidget *icon_box;

  GPtrArray *layout_spacers;

  guint nrows;

  GSettings *settings;
};

/* define the plugin */
XFCE_PANEL_DEFINE_PLUGIN (XAppStatusPlugin, xapp_status_plugin)

static gboolean xapp_status_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                                                        gint             size);
static void     xapp_status_plugin_screen_position_changed (XfcePanelPlugin   *panel_plugin,
                                                                   XfceScreenPosition position);
static void     xapp_status_plugin_orientation_changed (XfcePanelPlugin *panel_plugin,
                                                        GtkOrientation   orientation);
static void     xapp_status_plugin_mode_changed (XfcePanelPlugin     *panel_plugin,
                                                 XfcePanelPluginMode  mode);
static void     xapp_status_plugin_nrows_changed (XfcePanelPlugin *panel_plugin,
                                                  guint            rows);
static void     xapp_status_plugin_layout_icons  (XAppStatusPlugin *plugin);
static void     xapp_status_plugin_hide_spacers  (XAppStatusPlugin *plugin);
static gint     get_panel_nrows    (XAppStatusPlugin *plugin);
static gint     get_panel_row_size (XAppStatusPlugin *plugin);
static gint     get_color_icon_size (XAppStatusPlugin *plugin);
static gint     get_symbolic_icon_size (XAppStatusPlugin *plugin);


static void
xapp_status_plugin_init (XAppStatusPlugin *plugin)
{
  plugin->monitor = NULL;
  plugin->nrows = 1;
  plugin->layout_spacers = g_ptr_array_new ();
  plugin->lookup_table = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, NULL);
}

static const gchar *
non_null_string (const gchar *value)
{
    return value != NULL ? value : "";
}

static gchar *
get_unique_key (GDBusProxy *proxy)
{
    const gchar *name = xapp_status_icon_interface_get_name (XAPP_STATUS_ICON_INTERFACE (proxy));
    const gchar *path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (proxy));

    return g_strconcat (non_null_string (name), non_null_string (path), NULL);
}

static gint
compare_icons (gpointer a,
               gpointer b)
{
    XAppStatusIconInterface *proxy_a, *proxy_b;
    const gchar *icon_name_a, *icon_name_b, *name_a, *name_b;
    gboolean sym_a, sym_b;
    gint res;

    proxy_a = status_icon_get_proxy (STATUS_ICON (a));
    proxy_b = status_icon_get_proxy (STATUS_ICON (b));

    icon_name_a = xapp_status_icon_interface_get_icon_name (proxy_a);
    icon_name_b = xapp_status_icon_interface_get_icon_name (proxy_b);

    sym_a = icon_name_a != NULL && g_strstr_len (icon_name_a, -1, "symbolic") != NULL;
    sym_b = icon_name_b != NULL && g_strstr_len (icon_name_b, -1, "symbolic") != NULL;

    if (sym_a && !sym_b)
    {
        return 1;
    }

    if (sym_b && !sym_a)
    {
        return -1;
    }

    name_a = xapp_status_icon_interface_get_name (proxy_a);
    name_b = xapp_status_icon_interface_get_name (proxy_b);

    res = g_utf8_collate (non_null_string (name_a),
                          non_null_string (name_b));

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
move_grid_child (XAppStatusPlugin *plugin,
                 GtkWidget        *child,
                 gint              column,
                 gint              row)
{
    gtk_container_child_set (GTK_CONTAINER (plugin->icon_box),
                             child,
                             "left-attach", column,
                             "top-attach", row,
                             "width", 1,
                             "height", 1,
                             NULL);
}

static void
xapp_status_plugin_ensure_spacers (XAppStatusPlugin *plugin,
                                   gint              n_spacers)
{
    while ((gint) plugin->layout_spacers->len < n_spacers)
    {
        GtkWidget *spacer;

        spacer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_no_show_all (spacer, TRUE);
        gtk_widget_set_can_focus (spacer, FALSE);
        gtk_widget_set_size_request (spacer, 0, 0);
        gtk_grid_attach (GTK_GRID (plugin->icon_box),
                         spacer,
                         0,
                         g_hash_table_size (plugin->lookup_table) + plugin->layout_spacers->len + 1,
                         1,
                         1);

        g_ptr_array_add (plugin->layout_spacers, spacer);
    }
}

static void
xapp_status_plugin_hide_spacers (XAppStatusPlugin *plugin)
{
    guint i;

    for (i = 0; i < plugin->layout_spacers->len; i++)
    {
        GtkWidget *spacer = g_ptr_array_index (plugin->layout_spacers, i);

        gtk_widget_hide (spacer);
        gtk_widget_set_size_request (spacer, 0, 0);
        move_grid_child (plugin, spacer, 0, 0);
    }
}

/* GtkGrid only keeps homogeneous tracks for occupied cells. When the first
 * panel row/column is not full yet, visible empty spacers keep icons at the
 * panel row size instead of stretching them across the available allocation.
 */
static void
xapp_status_plugin_reserve_empty_slots (XAppStatusPlugin *plugin,
                                        GtkOrientation    panel_orientation,
                                        gint              visible_icons,
                                        gint              nrows,
                                        gint              icon_size)
{
    gint slot, spacer_index;

    if (visible_icons == 0 || visible_icons >= nrows)
    {
        return;
    }

    xapp_status_plugin_ensure_spacers (plugin, nrows - visible_icons);

    spacer_index = 0;
    for (slot = visible_icons; slot < nrows; slot++, spacer_index++)
    {
        GtkWidget *spacer = g_ptr_array_index (plugin->layout_spacers, spacer_index);
        gint column, row;

        if (panel_orientation == GTK_ORIENTATION_VERTICAL)
        {
            column = slot;
            row = 0;
        }
        else
        {
            column = 0;
            row = slot;
        }

        move_grid_child (plugin, spacer, column, row);
        gtk_widget_set_size_request (spacer, icon_size, icon_size);
        gtk_widget_show (spacer);
    }
}

static gboolean
status_icon_should_reserve_slot (StatusIcon *icon)
{
    XAppStatusIconInterface *proxy;

    proxy = status_icon_get_proxy (icon);

    return xapp_status_icon_interface_get_visible (proxy);
}

static void
xapp_status_plugin_layout_icons (XAppStatusPlugin *plugin)
{
    XfcePanelPlugin *panel_plugin = XFCE_PANEL_PLUGIN (plugin);
    GList *icons, *iter;
    GtkOrientation panel_orientation;
    gint nrows, n_icons, icon_size, color_icon_size, symbolic_icon_size, index, visible_index;

    icons = g_hash_table_get_values (plugin->lookup_table);

    if (!icons)
    {
        xapp_status_plugin_hide_spacers (plugin);
        gtk_widget_queue_resize (GTK_WIDGET (panel_plugin));
        return;
    }

    icons = g_list_sort (icons, (GCompareFunc) compare_icons);
    nrows = get_panel_nrows (plugin);
    n_icons = g_list_length (icons);
    icon_size = get_panel_row_size (plugin);
    color_icon_size = get_color_icon_size (plugin);
    symbolic_icon_size = get_symbolic_icon_size (plugin);
    panel_orientation = xfce_panel_plugin_get_orientation (panel_plugin);

    index = 0;
    for (iter = icons; iter != NULL; iter = iter->next, index++)
    {
        move_grid_child (plugin, GTK_WIDGET (iter->data), 0, n_icons + index);
    }

    xapp_status_plugin_hide_spacers (plugin);

    visible_index = 0;
    for (iter = icons; iter != NULL; iter = iter->next)
    {
        StatusIcon *icon = STATUS_ICON (iter->data);
        gint column, row;

        if (!status_icon_should_reserve_slot (icon))
        {
            gtk_widget_hide (GTK_WIDGET (icon));
            gtk_widget_set_size_request (GTK_WIDGET (icon), 0, 0);
            move_grid_child (plugin, GTK_WIDGET (icon), 0, 0);
            continue;
        }

        gtk_widget_show (GTK_WIDGET (icon));

        if (panel_orientation == GTK_ORIENTATION_VERTICAL)
        {
            column = visible_index % nrows;
            row = visible_index / nrows;
        }
        else
        {
            column = visible_index / nrows;
            row = visible_index % nrows;
        }

        move_grid_child (plugin, GTK_WIDGET (icon), column, row);

        gtk_widget_set_size_request (GTK_WIDGET (icon), icon_size, icon_size);
        status_icon_set_size (icon, color_icon_size, symbolic_icon_size);
        visible_index++;
    }

    xapp_status_plugin_reserve_empty_slots (plugin,
                                            panel_orientation,
                                            visible_index,
                                            nrows,
                                            icon_size);

    g_list_free (icons);
    gtk_widget_queue_resize (GTK_WIDGET (panel_plugin));
}

static void
sort_icons (XAppStatusPlugin *plugin)
{
    xapp_status_plugin_layout_icons (plugin);
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

    gtk_grid_attach (GTK_GRID (plugin->icon_box),
                     GTK_WIDGET (icon),
                     0,
                     g_hash_table_size (plugin->lookup_table),
                     1,
                     1);

    g_hash_table_insert (plugin->lookup_table,
                         g_strdup (key),
                         icon);

    g_free (key);

    g_signal_connect_swapped (icon, "re-sort", G_CALLBACK (sort_icons), plugin);
    g_signal_connect_object (proxy, "notify::icon-name", G_CALLBACK (sort_icons), plugin, G_CONNECT_SWAPPED);
    g_signal_connect_object (proxy, "notify::visible", G_CALLBACK (sort_icons), plugin, G_CONNECT_SWAPPED);
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
xapp_status_plugin_about (XfcePanelPlugin *plugin)
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

static gint
get_panel_nrows (XAppStatusPlugin *plugin)
{
    if (plugin->nrows == 0)
    {
        plugin->nrows = MAX (xfce_panel_plugin_get_nrows (XFCE_PANEL_PLUGIN (plugin)), 1u);
    }

    return (gint) plugin->nrows;
}

static gint
get_panel_row_size (XAppStatusPlugin *plugin)
{
    return MAX (xfce_panel_plugin_get_size (XFCE_PANEL_PLUGIN (plugin)) / get_panel_nrows (plugin), 1);
}

static gint
get_color_icon_size (XAppStatusPlugin *plugin)
{
    gint size, row_size;

    size = g_settings_get_int (plugin->settings, KEY_COLOR_ICON_SIZE);
    row_size = get_panel_row_size (plugin);

    if (size > 0 && size < row_size)
    {
        return size;
    }

    if (row_size < 22)
        return 16;
    else
    if (row_size < 24)
        return 22;
    else
    if (row_size < 32)
        return 24;
    else
    if (row_size < 48)
        return 32;

    return 48;
}

static gint
get_symbolic_icon_size (XAppStatusPlugin *plugin)
{
    gint size, row_size;

    size = g_settings_get_int (plugin->settings, KEY_SYMBOLIC_ICON_SIZE);
    row_size = get_panel_row_size (plugin);

    if (size > 0 && size < row_size)
    {
        return size;
    }

    return MAX (row_size - 4, 1);
}

static void
xapp_status_plugin_construct (XfcePanelPlugin *panel_plugin)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);

    plugin->settings = g_settings_new (SETTINGS_SCHEMA);
    plugin->nrows = MAX (xfce_panel_plugin_get_nrows (panel_plugin), 1u);

    plugin->icon_box = gtk_grid_new ();

    gtk_grid_set_row_homogeneous (GTK_GRID (plugin->icon_box), TRUE);
    gtk_grid_set_column_homogeneous (GTK_GRID (plugin->icon_box), TRUE);
    gtk_widget_set_halign (plugin->icon_box, GTK_ALIGN_FILL);
    gtk_widget_set_valign (plugin->icon_box, GTK_ALIGN_FILL);

    gtk_widget_show (plugin->icon_box);
    gtk_container_set_border_width (GTK_CONTAINER (plugin->icon_box),
                                    INDICATOR_BOX_BORDER);

    gtk_container_add (GTK_CONTAINER (plugin),
                       plugin->icon_box);

    gtk_widget_show_all (GTK_WIDGET (plugin));

    xfce_panel_plugin_set_small (panel_plugin, FALSE);

    plugin->monitor = xapp_status_icon_monitor_new ();

    g_signal_connect (plugin->monitor,
                      "icon-added",
                      G_CALLBACK (on_icon_added),
                      plugin);


    g_signal_connect (plugin->monitor,
                      "icon-removed",
                      G_CALLBACK (on_icon_removed),
                      plugin);

    xapp_status_plugin_screen_position_changed (panel_plugin,
                                                xfce_panel_plugin_get_screen_position (panel_plugin));

    xfce_panel_plugin_menu_show_configure (panel_plugin);
    xfce_panel_plugin_menu_show_about (panel_plugin);
}

/* This is our dispose */
static void
xapp_status_plugin_free_data (XfcePanelPlugin *panel_plugin)
{
  XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);

  g_clear_object (&plugin->monitor);
  g_ptr_array_free (plugin->layout_spacers, TRUE);
  g_hash_table_destroy (plugin->lookup_table);
  g_clear_object (&plugin->settings);
}

static void
xapp_status_plugin_screen_position_changed (XfcePanelPlugin   *panel_plugin,
                                                   XfceScreenPosition position)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);
    GtkPositionType xapp_orientation = GTK_POS_TOP;
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
    }
    else
    if (xfce_screen_position_is_right (position))
    {
        xapp_orientation = GTK_POS_RIGHT;
    }

    g_hash_table_iter_init (&iter, plugin->lookup_table);

    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        StatusIcon *icon = STATUS_ICON (value);

        status_icon_set_orientation (icon, xapp_orientation);
    }

    xapp_status_plugin_layout_icons (plugin);
}

static gboolean
xapp_status_plugin_size_changed (XfcePanelPlugin *panel_plugin,
                                        gint             size)
{
    (void) size;
    xapp_status_plugin_layout_icons (XAPP_STATUS_PLUGIN (panel_plugin));

    return TRUE;
}

static void
xapp_status_plugin_orientation_changed (XfcePanelPlugin *panel_plugin,
                                        GtkOrientation   orientation)
{
    (void) orientation;
    xapp_status_plugin_layout_icons (XAPP_STATUS_PLUGIN (panel_plugin));
}

static void
xapp_status_plugin_mode_changed (XfcePanelPlugin     *panel_plugin,
                                 XfcePanelPluginMode  mode)
{
    (void) mode;
    xapp_status_plugin_layout_icons (XAPP_STATUS_PLUGIN (panel_plugin));
}

static void
xapp_status_plugin_nrows_changed (XfcePanelPlugin *panel_plugin,
                                  guint            rows)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);

    plugin->nrows = MAX (rows, 1u);
    xapp_status_plugin_size_changed (panel_plugin,
                                     xfce_panel_plugin_get_size (panel_plugin));
}

static void
size_combo_changed (GtkComboBox      *combo,
                    XAppStatusPlugin *plugin,
                    const gchar      *key)
{
    GtkTreeIter active_iter;

    if (gtk_combo_box_get_active_iter (combo, &active_iter))
    {
        gint size = 0;
        gtk_tree_model_get (gtk_combo_box_get_model (combo),
                            &active_iter,
                            1, &size,
                            -1);

        g_settings_set_int (plugin->settings, key, size);
    }

    xapp_status_plugin_size_changed (XFCE_PANEL_PLUGIN (plugin),
                                     xfce_panel_plugin_get_size (XFCE_PANEL_PLUGIN (plugin)));
}

static void
color_combo_changed (GtkComboBox *combo, XAppStatusPlugin *plugin)
{
    size_combo_changed (combo, plugin, KEY_COLOR_ICON_SIZE);
}

static void
symbolic_combo_changed (GtkComboBox *combo, XAppStatusPlugin *plugin)
{
    size_combo_changed (combo, plugin, KEY_SYMBOLIC_ICON_SIZE);
}

static void
initialize_combo (XAppStatusPlugin *plugin,
                  GtkWidget        *combo,
                  const gchar      *key)
{
    gchar *id;
    gint size = g_settings_get_int (plugin->settings, key);

    id = g_strdup_printf ("%d", size);
    gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo), id);
}

static void
xapp_status_plugin_configure_plugin (XfcePanelPlugin *panel_plugin)
{
    XAppStatusPlugin *plugin = XAPP_STATUS_PLUGIN (panel_plugin);
    GtkBuilder *builder = gtk_builder_new_from_file (APP_DATADIR "/configure.glade");
    GtkWidget *dialog;
    GtkWidget *combo;

    dialog = GTK_WIDGET (gtk_builder_get_object (builder, "dialog"));

    combo = GTK_WIDGET (gtk_builder_get_object (builder, "color_combo"));
    initialize_combo (plugin, combo, KEY_COLOR_ICON_SIZE);
    g_signal_connect (combo, "changed", G_CALLBACK (color_combo_changed), plugin);
    combo = GTK_WIDGET (gtk_builder_get_object (builder, "symbolic_combo"));
    initialize_combo (plugin, combo, KEY_SYMBOLIC_ICON_SIZE);
    g_signal_connect (combo, "changed", G_CALLBACK (symbolic_combo_changed), plugin);

    gtk_widget_show_all (dialog);
    gtk_dialog_run (GTK_DIALOG (dialog));

    gtk_widget_destroy (dialog);
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
  plugin_class->orientation_changed = xapp_status_plugin_orientation_changed;
  plugin_class->mode_changed = xapp_status_plugin_mode_changed;
  plugin_class->nrows_changed = xapp_status_plugin_nrows_changed;
  plugin_class->configure_plugin = xapp_status_plugin_configure_plugin;
  plugin_class->about = xapp_status_plugin_about;

    /* Initialize gettext support */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
}
