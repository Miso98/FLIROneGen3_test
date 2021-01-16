/*
 * Copyright (C) 2021 Nicole Faerber <nicole.faerber@dpin.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <gtk/gtk.h>

#include "palettes/15.h"
#include "palettes/17.h"
#include "palettes/7.h"
#include "palettes/85.h"
#include "palettes/92.h"
#include "palettes/Grayscale.h"
#include "palettes/Grey.h"
#include "palettes/Iron2.h"
#include "palettes/Iron_Black.h"
#include "palettes/Rainbow.h"

static GtkWidget *window = NULL;
static GtkWidget *image_darea = NULL;
static cairo_surface_t *surface = NULL;

// internal frame buffer with 640x480 pixels of 4 byte each,
// first byte unused, R, G, B 0x00RRGGBB
unsigned char fbuffer[640*480*4];
unsigned char *fbdata;
unsigned char *color_palette;
gboolean flir_run = FALSE;

gpointer cam_thread_main(gpointer user_data);


static gboolean
configure_event (GtkWidget         *widget,
                          GdkEventConfigure *event,
                          gpointer           data)
{
GtkAllocation allocation;
int stride;

	if (surface)
		cairo_surface_destroy (surface);

	gtk_widget_get_allocation (widget, &allocation);
	stride = cairo_format_stride_for_width (CAIRO_FORMAT_RGB24, 640);
	// g_printerr("stride %d\n", stride);
	surface = cairo_image_surface_create_for_data (fbuffer,
                                     CAIRO_FORMAT_RGB24,
                                     640,
                                     480,
                                     stride);
	fbdata = cairo_image_surface_get_data(surface);

	g_printerr("configure event %d x %d\n", allocation.width, allocation.height);

	/* We've handled the configure event, no need for further processing. */
	return TRUE;
}


static gboolean
draw_event (GtkWidget *widget,
               cairo_t   *cr,
               gpointer   data)
{
	// g_printerr("draw event\n");

	cairo_set_source_surface (cr, surface, 0, 0);
	cairo_paint (cr);

	return FALSE;
}

void
update_fb(void)
{
#if 0
unsigned int x,y;
unsigned int *pdata;
static unsigned char cnt=0;

	cnt+=0x0f;
	// g_printerr("cnt=%d\n", cnt);
	for (y=0; y<480; y++) {
		for (x=0; x<640; x++) {
			pdata = (unsigned int *)&fbdata[(y*640*4)+(x*4)];
			*pdata = (cnt << 16) | (x << 9) | y;
		}
	}
#endif
	gtk_widget_queue_draw(image_darea);
}

void
start_clicked(GtkWidget *button, gpointer user_data)
{
	flir_run = TRUE;
	g_thread_new ("CAM thread", cam_thread_main, NULL);
}

void
stop_clicked(GtkWidget *button, gpointer user_data)
{
	flir_run = FALSE;
}

static void
close_window (void)
{
	// clean up and quit
	window = NULL;
	gtk_main_quit();
}

gboolean
handle_timeout (gpointer user_data)
{
	update_fb();

	return TRUE;
}

void
palette_changed (GtkComboBox *widget, gpointer user_data)
{
int act;

	act = gtk_combo_box_get_active(widget);
	if (act < 0) {
		g_printerr("oops, palette selection = %d\n", act);
	} else {
		if (act == 0) color_palette = palette_7;
		if (act == 1) color_palette = palette_15;
		if (act == 2) color_palette = palette_17;
		if (act == 3) color_palette = palette_85;
		if (act == 4) color_palette = palette_92;
		if (act == 5) color_palette = palette_Grayscale;
		if (act == 6) color_palette = palette_Grey;
		if (act == 7) color_palette = palette_Iron2;
		if (act == 8) color_palette = palette_Iron_Black;
		if (act == 9) color_palette = palette_Rainbow;
	};
}


GtkWidget *
create_main_window ()
{
GtkWidget *box;
GtkWidget *hbox;
GtkWidget *w;
// GtkWidget *da;

	if (!window) {
		window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title (GTK_WINDOW (window), "FLIR One");

		g_signal_connect (window, "destroy",
			G_CALLBACK (close_window), NULL); 

		box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_container_add (GTK_CONTAINER (window), box);

		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		gtk_container_add (GTK_CONTAINER (box), hbox);

		w = gtk_button_new_with_label("Start");
		gtk_container_add (GTK_CONTAINER (hbox), w);

		g_signal_connect (w, "clicked",
			G_CALLBACK (start_clicked), NULL);

		w = gtk_button_new_with_label("Stop");
		gtk_container_add (GTK_CONTAINER (hbox), w);

		g_signal_connect (w, "clicked",
			G_CALLBACK (stop_clicked), NULL);

		// drop down for color palettes
		w = gtk_combo_box_text_new();
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "7");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "15");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "17");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "85");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "92");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "Grayscale");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "Grey");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "Iron 2");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "Iron Black");
		gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT(w), NULL, "Rainbow");
		gtk_combo_box_set_active (GTK_COMBO_BOX(w), 9);
		gtk_container_add (GTK_CONTAINER (hbox), w);
		color_palette = palette_Rainbow;
		g_signal_connect (w, "changed",
			G_CALLBACK (palette_changed), NULL);

		image_darea = gtk_drawing_area_new ();
		/* set a minimum size */
		gtk_widget_set_size_request (image_darea, 640, 480);

		gtk_container_add (GTK_CONTAINER (box), image_darea);

		g_signal_connect (image_darea, "draw",
			G_CALLBACK (draw_event), NULL);
		g_signal_connect (image_darea,"configure-event",
			G_CALLBACK (configure_event), NULL);

		// g_timeout_add_seconds(1, handle_timeout, NULL);

		gtk_widget_show_all(window);
	}

	return window;
}

int
main(int argc, char **argv)
{
	gtk_init(&argc, &argv);

	create_main_window();

	gtk_main();

return 0;
}

