// gcc `pkg-config --cflags gtk+-3.0` -c flirgtk.c
// gcc -o flirgtk flirgtk.o `pkg-config --libs gtk+-3.0`

#include <gtk/gtk.h>

static GtkWidget *window = NULL;
static cairo_surface_t *surface = NULL;


static gboolean
configure_event (GtkWidget         *widget,
                          GdkEventConfigure *event,
                          gpointer           data)
{
GtkAllocation allocation;
cairo_t *cr;

    if (surface)
        cairo_surface_destroy (surface);

    gtk_widget_get_allocation (widget, &allocation);
    surface = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
                                               CAIRO_CONTENT_COLOR,
                                               allocation.width,
                                               allocation.height);
    // cr = cairo_create (surface);

    g_printerr("configure event %d x %d\n", allocation.width, allocation.height);

    /* We've handled the configure event, no need for further processing. */
    return TRUE;
}

#if 0
int stride;
unsigned char *data;
cairo_surface_t *surface;

stride = cairo_format_stride_for_width (format, width);
data = malloc (stride * height);
surface = cairo_image_surface_create_for_data (data, format,
					  width, height,
					  stride);
#endif

static gboolean
draw_event (GtkWidget *widget,
               cairo_t   *cr,
               gpointer   data)
{
    g_printerr("draw event\n");

    cairo_set_source_surface (cr, surface, 0, 0);
    cairo_paint (cr);

    return FALSE;
}

static void
close_window (void)
{
    window = NULL;
}


GtkWidget *
do_drawingarea ()
{
GtkWidget *da;

    if (!window) {
        window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title (GTK_WINDOW (window), "FLIR One");

        g_signal_connect (window, "destroy",
                        G_CALLBACK (close_window), NULL); 
        da = gtk_drawing_area_new ();
        /* set a minimum size */
        gtk_widget_set_size_request (da, 160, 120);

        gtk_container_add (GTK_CONTAINER (window), da);

        g_signal_connect (da, "draw",
                        G_CALLBACK (draw_event), NULL);
        g_signal_connect (da,"configure-event",
                        G_CALLBACK (configure_event), NULL);
        gtk_widget_show_all(window);
    }
    return window;
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    do_drawingarea();
    gtk_main();

return 0;
}
