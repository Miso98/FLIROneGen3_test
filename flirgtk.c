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

#include <sys/types.h>
       #include <sys/stat.h>
       #include <fcntl.h>


#include "cairo_jpg/src/cairo_jpg.h"

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
unsigned char fbuffer[640*500*4];
unsigned char *fbdata;
unsigned char *color_palette;
gboolean pending=FALSE;
gboolean ircam=TRUE;
gboolean viscam=FALSE;
gboolean flir_run = FALSE;

gpointer cam_thread_main(gpointer user_data);

extern double t_min, t_max, t_center;
#define BUF85SIZE 1048576
unsigned char *jpeg_buffer=NULL;
unsigned int jpeg_size=0;


void draw_palette(void);


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
	// g_printerr("configure event %d x %d\n", allocation.width, allocation.height);
	stride = cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, 640);
	// g_printerr("stride %d\n", stride);
	surface = cairo_image_surface_create_for_data (fbuffer,
                                     CAIRO_FORMAT_ARGB32,
                                     640,
                                     500,
                                     stride);
	fbdata = cairo_image_surface_get_data(surface);
	// memset(fbdata, 0x00, 640*500*4);
	draw_palette();

	/* We've handled the configure event, no need for further processing. */
	return TRUE;
}


static gboolean
draw_event (GtkWidget *widget,
               cairo_t   *cr,
               gpointer   data)
{
char tdisp[16];
cairo_surface_t *jpeg_surface;
static int fcnt=0;

	// g_printerr("draw event\n");
	cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.7);
	cairo_paint (cr);

	// first draw the frame buffer containing the IR frame
#if 1
	if (ircam) {
		cairo_set_source_surface (cr, surface, 0, 0);
		cairo_set_operator (cr, CAIRO_OPERATOR_OVERLAY);
		cairo_paint (cr);
	}
#endif
#if 1
	if (jpeg_size != 0 && jpeg_buffer != NULL) {
		// g_printerr(" draw event %d\n", jpeg_size);
		if (viscam) {
			jpeg_surface=cairo_image_surface_create_from_jpeg_mem(jpeg_buffer, jpeg_size);
			cairo_save(cr);
			cairo_scale (cr, (1./2.25), (1./2.25));
			cairo_set_source_surface (cr, jpeg_surface, 0, 0);
			cairo_set_operator (cr, CAIRO_OPERATOR_OVERLAY);
			cairo_paint (cr);
			cairo_restore(cr);
			cairo_surface_destroy (jpeg_surface);
		}
		jpeg_size=0;
		jpeg_buffer=NULL;
	}
#endif
#if 1
	// then draw decoration on top
//	cairo_scale (cr, 1, 1);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

	// crosshair in the center
	cairo_set_line_width (cr, 3);
	cairo_set_source_rgb (cr, 0, 0, 0);
	cairo_move_to(cr, 320, 200);
	cairo_line_to(cr, 320, 280);
	cairo_stroke (cr);
	cairo_move_to(cr, 280, 240);
	cairo_line_to(cr, 360, 240);
	cairo_stroke (cr);
	cairo_set_line_width (cr, 1);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_move_to(cr, 320, 200);
	cairo_line_to(cr, 320, 280);
	cairo_stroke (cr);
	cairo_move_to(cr, 280, 240);
	cairo_line_to(cr, 360, 240);
	cairo_stroke (cr);

	// print center temperature near crosshair
	snprintf(tdisp, 16, "%.1f°C", t_center);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_select_font_face (cr, "Sans",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 24);
	// cairo_text_extents (cr, "a", &te);
	cairo_move_to (cr, 330, 220);
	cairo_show_text (cr, tdisp);

	// update palette scale temperature range
	snprintf(tdisp, 16, "%.1f°C", t_min);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_select_font_face (cr, "Sans",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 18);
	cairo_move_to (cr, 102, 496);
	cairo_show_text (cr, tdisp);

	snprintf(tdisp, 16, "%.1f°C", t_max);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_select_font_face (cr, "Sans",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 18);
	cairo_move_to (cr, 440, 496);
	cairo_show_text (cr, tdisp);
#endif
	pending = FALSE;

	return FALSE;
}


void
update_fb(void)
{
	if (!pending) {
		pending=TRUE;
		gtk_widget_queue_draw(image_darea);
	}
}

void
store_shot_clicked(GtkWidget *button, gpointer user_data)
{
//cairo_surface_t *cairo_get_target (cairo_t *cr);
	cairo_surface_write_to_png (surface, "shot.png");
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

void
ircam_clicked(GtkWidget *button, gpointer user_data)
{
	ircam = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
}

void
viscam_clicked(GtkWidget *button, gpointer user_data)
{
	viscam = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
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

// 256 colors (8bit), two hor pixel per color
// piture width = 640, center scale, i.e. start at 64
void
draw_palette(void)
{
unsigned int *p1, *pc;
int x,y;

	y=481; // leave one line blank/black
	for (x=0; x<256; x++) {
		fbdata[4*y * 640 + (x+174)*4] = color_palette[3 * x + 2];  // B
		fbdata[(4*y * 640 + (x+174)*4)+1] = color_palette[3 * x + 1]; // G
		fbdata[(4*y * 640 + (x+174)*4)+2] = color_palette[3 * x]; // R
	}
	y=481;
	p1 = (unsigned int *)&fbdata[4*y * 640]; // pointer to start of line
	for (y=482; y<500; y++) {
		pc = (unsigned int *)&fbdata[4*y * 640]; // pointer to start of copy line
		memcpy(pc,p1,640*4);
	}
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
		draw_palette();
	};
}


GtkWidget *
create_main_window ()
{
GtkWidget *box;
GtkWidget *hbox;
GtkWidget *w;
// GtkWidget *da;

	// default color palette
	color_palette = palette_Rainbow;

	if (!window) {
		window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		gtk_window_set_title (GTK_WINDOW (window), "FLIR One");

		g_signal_connect (window, "destroy",
			G_CALLBACK (close_window), NULL); 

		box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		gtk_container_add (GTK_CONTAINER (window), box);

		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		gtk_container_add (GTK_CONTAINER (box), hbox);

		// 48 GTK_ICON_SIZE_DIALOG
		// 32 GTK_ICON_SIZE_DND
		// media-playback-start
		// w = gtk_button_new_with_label("Start");
		w = gtk_button_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_DND);
		gtk_container_add (GTK_CONTAINER (hbox), w);

		g_signal_connect (w, "clicked",
			G_CALLBACK (start_clicked), NULL);

		// media-playback-stop
		// w = gtk_button_new_with_label("Stop");
		w = gtk_button_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_DND);
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
		g_signal_connect (w, "changed",
			G_CALLBACK (palette_changed), NULL);

		w = gtk_toggle_button_new_with_label("IR");
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), TRUE);
		gtk_container_add (GTK_CONTAINER (hbox), w);
		g_signal_connect (w, "clicked",
			G_CALLBACK (ircam_clicked), NULL);

		w = gtk_toggle_button_new_with_label("Vis");
		gtk_container_add (GTK_CONTAINER (hbox), w);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), FALSE);
		g_signal_connect (w, "clicked",
			G_CALLBACK (viscam_clicked), NULL);


		image_darea = gtk_drawing_area_new ();
		/* set a minimum size */
		gtk_widget_set_size_request (image_darea, 640, 500);

		gtk_container_add (GTK_CONTAINER (box), image_darea);

		g_signal_connect (image_darea, "draw",
			G_CALLBACK (draw_event), NULL);
		g_signal_connect (image_darea,"configure-event",
			G_CALLBACK (configure_event), NULL);

		// camera-photo
		// w = gtk_button_new_with_label("Shot");
		w = gtk_button_new_from_icon_name("camera-photo", GTK_ICON_SIZE_DND);
		gtk_container_add (GTK_CONTAINER (box), w);

		g_signal_connect (w, "clicked",
			G_CALLBACK (store_shot_clicked), NULL);

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

