/* Based on gtkstackicon.c */

#include <json-glib/json-glib.h>

#include "status-icon.h"
#include <libxapp/xapp-status-icon.h>

enum
{
    RE_SORT,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0, };

struct _StatusIcon
{
    GtkToggleButton parent_instance;

    gint size;  /* Size of the panel to calculate from */
    GtkPositionType orientation; /* Orientation of the panel */
    const gchar *name;
    const gchar *process_name;

    XAppStatusIconInterface *proxy; /* The proxy for a remote XAppStatusIcon */

    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;

    gboolean highlight_both_menus;
    gboolean menu_opened;

    GCancellable *image_load_cancellable;
};

G_DEFINE_TYPE (StatusIcon, status_icon, GTK_TYPE_TOGGLE_BUTTON)

#define VERTICAL_PANEL(o) (o == GTK_POS_LEFT || o == GTK_POS_RIGHT)

typedef struct {
  gchar *path;
  gint   width, height, scale;
} ImageFromFileAsyncData;

static void
sortable_name_changed (gpointer data)
{
    StatusIcon *icon = STATUS_ICON (data);

    g_signal_emit (icon, signals[RE_SORT], 0);
}

static void
on_image_from_file_data_destroy (gpointer data)
{
  ImageFromFileAsyncData *d = (ImageFromFileAsyncData *)data;
  g_free (d->path);
  g_free (d);
}

static void
on_image_from_file_loaded (GObject      *source,
                           GAsyncResult *res,
                           gpointer      user_data)
{
    StatusIcon *icon = STATUS_ICON (user_data);
    GTask *task = G_TASK (res);
    GdkPixbuf *pixbuf;
    cairo_surface_t *surface;
    GError *error;
    ImageFromFileAsyncData *data;

    data = (ImageFromFileAsyncData *) g_task_get_task_data (task);
    error = NULL;

    pixbuf = g_task_propagate_pointer (task, &error);

    g_clear_object (&icon->image_load_cancellable);

    if (error)
    {
        if (error->code != G_IO_ERROR_CANCELLED)
        {
            g_warning ("Could not load image from file: %s\n", error->message);
            g_error_free (error);
        }

        return;
    }

    surface = gdk_cairo_surface_create_from_pixbuf (pixbuf,
                                                    data->scale,
                                                    gtk_widget_get_window (GTK_WIDGET (icon)));

    g_object_unref (pixbuf);

    gtk_image_set_pixel_size (GTK_IMAGE (icon->image), -1);
    gtk_image_set_from_surface (GTK_IMAGE (icon->image), surface);

    cairo_surface_destroy (surface);
}

static void
load_image_from_file_thread (GTask        *task,
                             gpointer      source,
                             gpointer      task_data,
                             GCancellable *cancellable)
{
    ImageFromFileAsyncData *data;
    GdkPixbuf *pixbuf;
    GError *error;

    data = task_data;
    error = NULL;

    /* Pixbuf size is multiplied by the ui scale */
    pixbuf = gdk_pixbuf_new_from_file_at_scale (data->path,
                                                data->width > 0 ? data->width * data->scale : -1,
                                                data->height > 0 ? data->height * data->scale : -1,
                                                TRUE,
                                                &error);

    if (error)
    {
        g_task_return_error (task, error);
    }

    g_task_return_pointer (task, pixbuf, NULL);
}

static void
load_file_based_image (StatusIcon  *icon,
                       const gchar *path)
{

    ImageFromFileAsyncData *data;
    GTask *result;

    data = g_new0 (ImageFromFileAsyncData, 1);
    // I can't imagine supporting a vertical panel somehow.. but it's here in case.
    data->width = -1;
    data->height = icon->size;
    data->scale = gtk_widget_get_scale_factor (GTK_WIDGET (icon));
    data->path = g_strdup (path);

    icon->image_load_cancellable = g_cancellable_new ();

    result = g_task_new (icon,
                         icon->image_load_cancellable,
                         on_image_from_file_loaded,
                         icon);

    g_task_set_task_data (result, data, on_image_from_file_data_destroy);
    g_task_run_in_thread (result, load_image_from_file_thread);

    g_object_unref (result);
}

static void
update_image (StatusIcon *icon)
{
    g_return_if_fail (STATUS_IS_ICON (icon));

    GIcon *gicon;
    const gchar *icon_name;

    icon_name = xapp_status_icon_interface_get_icon_name (XAPP_STATUS_ICON_INTERFACE (icon->proxy));
    gicon = NULL;

    if (!icon_name)
    {
        return;
    }

    if (g_file_test (icon_name, G_FILE_TEST_EXISTS))
    {
        if (g_str_has_suffix (icon_name, "symbolic") || VERTICAL_PANEL (icon->orientation))
        {
            GFile *icon_file = g_file_new_for_path (icon_name);
            gicon = G_ICON (g_file_icon_new (icon_file));
            g_object_unref (icon_file);
        }
        else
        {
            load_file_based_image(icon, icon_name);
            return;
        }
    }
    else
    {
        GtkIconTheme *theme = gtk_icon_theme_get_default ();

        if (gtk_icon_theme_has_icon (theme, icon_name))
        {
            gicon = G_ICON (g_themed_icon_new (icon_name));

        }
    }

    gtk_image_set_pixel_size (GTK_IMAGE (icon->image), icon->size);

    if (gicon)
    {
        gtk_image_set_from_gicon (GTK_IMAGE (icon->image), G_ICON (gicon), GTK_ICON_SIZE_MENU);

        g_object_unref (gicon);
    }
    else
    {
        gtk_image_set_from_icon_name (GTK_IMAGE (icon->image), "image-missing", GTK_ICON_SIZE_MENU);
    }
}

static void
calculate_proxy_args (StatusIcon *icon,
                      gint       *x,
                      gint       *y)
{
    GdkWindow *window;
    GtkAllocation allocation;
    gint final_x, final_y, wx, wy;

    final_x = 0;
    final_y = 0;

    window = gtk_widget_get_window (GTK_WIDGET (icon));

    gdk_window_get_origin (window, &wx, &wy);
    gtk_widget_get_allocation (GTK_WIDGET (icon), &allocation);

    switch (icon->orientation)
    {
        case GTK_POS_TOP:
            final_x = wx + allocation.x;
            final_y = wy + allocation.y + INDICATOR_BOX_BORDER_COMP;
            break;
        case GTK_POS_BOTTOM:
            final_x = wx + allocation.x;
            final_y = wy + allocation.y - INDICATOR_BOX_BORDER_COMP;
            break;
        case GTK_POS_LEFT:
            final_x = wx + allocation.x + allocation.width + INDICATOR_BOX_BORDER_COMP;
            final_y = wy + allocation.y;
            break;
        case GTK_POS_RIGHT:
            final_x = wx + allocation.x - INDICATOR_BOX_BORDER_COMP;
            final_y = wy + allocation.y;
            break;
        default:
            break;
    }

    *x = final_x;
    *y = final_y;
}

static void
update_orientation (StatusIcon *icon)
{
    switch (icon->orientation)
    {
        case GTK_POS_TOP:
        case GTK_POS_BOTTOM:
            gtk_orientable_set_orientation (GTK_ORIENTABLE (icon->box), GTK_ORIENTATION_HORIZONTAL);
            if (strlen (gtk_label_get_label (GTK_LABEL (icon->label))) > 0)
            {
                gtk_widget_set_visible (icon->label, TRUE);
                gtk_widget_set_margin_start (icon->label, VISIBLE_LABEL_MARGIN);
            }
            break;
        case GTK_POS_LEFT:
        case GTK_POS_RIGHT:
            gtk_orientable_set_orientation (GTK_ORIENTABLE (icon->box), GTK_ORIENTATION_VERTICAL);
            gtk_widget_set_visible (icon->label, FALSE);
            gtk_widget_set_margin_start (icon->label, 0);
            break;
    }
}

static void
menu_visible_changed (XAppStatusIconInterface *proxy,
                      GParamSpec              *pspec,
                      gpointer                 user_data)
{
    StatusIcon *icon = STATUS_ICON (user_data);
    gboolean menu_prop_is_opened;

    if (g_strcmp0 (g_param_spec_get_name (pspec), "primary-menu-is-open") == 0)
    {
        menu_prop_is_opened = xapp_status_icon_interface_get_primary_menu_is_open (proxy);
    }
    else
    {
        menu_prop_is_opened = xapp_status_icon_interface_get_secondary_menu_is_open (proxy);
    }

    if (!icon->menu_opened || !menu_prop_is_opened)
    {
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (icon), FALSE);
        return;
    }

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (icon), menu_prop_is_opened);
    icon->menu_opened = FALSE;
}

static gboolean
on_button_press_event (GtkWidget *widget,
                       GdkEvent  *event,
                       gpointer   user_data)
{
    StatusIcon *icon = STATUS_ICON (widget);
    gint x, y;

    x = 0;
    y = 0;

    icon->menu_opened = FALSE;

    calculate_proxy_args (icon, &x, &y);

    xapp_status_icon_interface_call_button_press (icon->proxy,
                                                  x, y,
                                                  event->button.button,
                                                  event->button.time,
                                                  icon->orientation,
                                                  NULL,
                                                  NULL,
                                                  NULL);

    return GDK_EVENT_STOP;
}

static gboolean
on_button_release_event (GtkWidget *widget,
                         GdkEvent  *event,
                         gpointer   user_data)
{
    StatusIcon *icon = STATUS_ICON (widget);
    gint x, y;

    x = 0;
    y = 0;

    calculate_proxy_args (icon, &x, &y);

    xapp_status_icon_interface_call_button_release (icon->proxy,
                                                    x, y,
                                                    event->button.button,
                                                    event->button.time,
                                                    icon->orientation,
                                                    NULL,
                                                    NULL,
                                                    NULL);

    if (event->button.button == GDK_BUTTON_PRIMARY ||
        (event->button.button == GDK_BUTTON_SECONDARY && icon->highlight_both_menus))
    {
        icon->menu_opened = TRUE;
    }

    return GDK_EVENT_PROPAGATE;
}

static gboolean
on_scroll_event (GtkWidget *widget,
                 GdkEvent  *event,
                 gpointer   user_data)
{
    StatusIcon *icon = STATUS_ICON (widget);
    GdkScrollDirection direction;
    XAppScrollDirection x_dir = XAPP_SCROLL_UP;
    gint delta = 0;

    if (gdk_event_get_scroll_direction (event, &direction))
    {
        x_dir = direction;

        switch (direction)
        {
            case GDK_SCROLL_UP:
                delta = -1;
                break;
            case GDK_SCROLL_DOWN:
                delta = 1;
                break;
            case GDK_SCROLL_LEFT:
                delta = -1;
                break;
            case GDK_SCROLL_RIGHT:
                delta = 1;
                break;
            default:
                break;
        }
    }

    if (delta != 0)
    {
        xapp_status_icon_interface_call_scroll (icon->proxy,
                                                delta,
                                                x_dir,
                                                event->button.time,
                                                NULL,
                                                NULL,
                                                NULL);
    }

    return GDK_EVENT_PROPAGATE;
}

static void
status_icon_init (StatusIcon *icon)
{
    GtkStyleContext *context;
    GtkCssProvider  *provider;
    icon->box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    gtk_widget_add_events (GTK_WIDGET (icon), GDK_SCROLL_MASK);

    gtk_container_add (GTK_CONTAINER (icon), icon->box);

    icon->image = gtk_image_new ();

    icon->label = gtk_label_new (NULL);
    gtk_widget_set_no_show_all (icon->label, TRUE);

    gtk_box_pack_start (GTK_BOX (icon->box), icon->image, TRUE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (icon->box), icon->label, FALSE, FALSE, 0);

    gtk_widget_set_can_default (GTK_WIDGET (icon), FALSE);
    gtk_widget_set_can_focus (GTK_WIDGET (icon), FALSE);
    gtk_button_set_relief (GTK_BUTTON (icon), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click (GTK_WIDGET (icon), FALSE);

    gtk_widget_set_name (GTK_WIDGET (icon), "xfce-panel-toggle-button");
    /* Make sure themes like Adwaita, which set excessive padding, don't cause the
       launcher buttons to overlap when panels have a fairly normal size */
    context = gtk_widget_get_style_context (GTK_WIDGET (icon));
    provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (provider, ".xfce4-panel button { padding: 1px; }", -1, NULL);
    gtk_style_context_add_provider (context,
                                    GTK_STYLE_PROVIDER (provider),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
status_icon_dispose (GObject *object)
{
    StatusIcon *icon = STATUS_ICON (object);

    g_clear_object (&icon->proxy);
    g_clear_object (&icon->image_load_cancellable);

    G_OBJECT_CLASS (status_icon_parent_class)->dispose (object);
}

static void
status_icon_finalize (GObject *object)
{
    G_OBJECT_CLASS (status_icon_parent_class)->finalize (object);
}

static void
status_icon_class_init (StatusIconClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = status_icon_dispose;
    object_class->finalize = status_icon_finalize;

    signals [RE_SORT] =
    g_signal_new ("re-sort",
                  STATUS_TYPE_ICON,
                  G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
bind_props_and_signals (StatusIcon *icon)
{
    guint flags = G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE;

    g_object_bind_property (icon->proxy, "label", icon->label, "label", flags);
    g_object_bind_property (icon->proxy, "tooltip-text", GTK_BUTTON (icon), "tooltip-text", flags);
    g_object_bind_property (icon->proxy, "visible", GTK_BUTTON (icon), "visible", flags);

    g_signal_connect (icon->proxy, "notify::primary-menu-is-open", G_CALLBACK (menu_visible_changed), icon);
    g_signal_connect (icon->proxy, "notify::secondary-menu-is-open", G_CALLBACK (menu_visible_changed), icon);
    g_signal_connect_swapped (icon->proxy, "notify::icon-name", G_CALLBACK (update_image), icon);
    g_signal_connect_swapped (icon->proxy, "notify::name", G_CALLBACK (sortable_name_changed), icon);

    g_signal_connect (GTK_WIDGET (icon), "button-press-event", G_CALLBACK (on_button_press_event), NULL);
    g_signal_connect (GTK_WIDGET (icon), "button-release-event", G_CALLBACK (on_button_release_event), NULL);
    g_signal_connect (GTK_WIDGET (icon), "scroll-event", G_CALLBACK (on_scroll_event), NULL);
}

static void
load_metadata (StatusIcon *icon)
{
    g_autoptr (JsonParser) parser = NULL;
    GError *error;
    JsonNode *root, *child;
    JsonObject *dict;
    JsonObjectIter iter;
    const gchar *data, *child_name;

    data = xapp_status_icon_interface_get_metadata (icon->proxy);

    if (data == NULL || data[0] == '\0')
    {
        return;
    }

    parser = json_parser_new ();
    error = NULL;

    if (!json_parser_load_from_data (parser, data, -1, &error))
    {
        g_warning ("Could not parse icon metadata: %s\n", error->message);
        g_error_free (error);
        return;
    }

    root = json_parser_get_root (parser);

    g_return_if_fail (JSON_NODE_TYPE (root) == JSON_NODE_OBJECT);

    dict = json_node_get_object (root);

    json_object_iter_init (&iter, dict);

    while (json_object_iter_next (&iter, &child_name, &child))
    {
        if (g_strcmp0 (child_name, "highlight-both-menus") == 0)
        {
            icon->highlight_both_menus = json_node_get_boolean (child);
        }
    }
}

void
status_icon_set_size (StatusIcon *icon, gint size)
{
    g_return_if_fail (STATUS_IS_ICON (icon));

    if (size % 2 != 0)
    {
        size--;
    }

    if (icon->size == size)
    {
        return;
    }

    icon->size = size;
    xapp_status_icon_interface_set_icon_size (icon->proxy, size);

    update_image (icon);
}

void
status_icon_set_orientation (StatusIcon *icon, GtkPositionType orientation)
{
    g_return_if_fail (STATUS_IS_ICON (icon));

    if (icon->orientation == orientation)
    {
        return;
    }

    icon->orientation = orientation;

    update_orientation (icon);
}

XAppStatusIconInterface *
status_icon_get_proxy (StatusIcon *icon)
{
    g_return_val_if_fail (STATUS_IS_ICON (icon), NULL);

    return icon->proxy;
}

StatusIcon *
status_icon_new (XAppStatusIconInterface *proxy,
                 gint                     icon_size)
{
    StatusIcon *icon = g_object_new (STATUS_TYPE_ICON, NULL);
    icon->proxy = g_object_ref (proxy);

    gtk_widget_show_all (GTK_WIDGET (icon));
    bind_props_and_signals (icon);
    load_metadata (icon);

    update_orientation (icon);
    status_icon_set_size (icon, icon_size);

    return icon;
}
