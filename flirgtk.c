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
#include <time.h>
// #include <limits.h>

#include "cam-thread.h"

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


// UI variables
static GtkWidget *window = NULL;
static GtkWidget *image_darea = NULL;
// static GtkApplication *gapp;
static GtkWidget *play_button, *stop_button;
 // we paint everything in here and then into the drawing area widget
static cairo_surface_t *psurface;
static double vis_surface_alpha=0.3;
static gboolean take_vis_shot=FALSE;


// variables to communicate with cam thread
gboolean pending=FALSE;
gboolean ircam=TRUE;
gboolean viscam=FALSE;
gboolean flir_run = FALSE;
unsigned char *color_palette;

gpointer cam_thread_main(gpointer user_data);

extern double t_min, t_max, t_center;
extern struct shutter_state_t shutter_state;
extern struct battery_state_t battery_state;

unsigned char *ir_buffer=NULL;
unsigned char *jpeg_buffer=NULL;
unsigned int jpeg_size=0;



static gboolean
configure_event (GtkWidget         *widget,
                          GdkEventConfigure *event,
                          gpointer           data)
{
//GtkAllocation allocation;

	// g_printerr("configure event %d x %d\n", allocation.width, allocation.height);
//	if (surface)
//		cairo_surface_destroy (surface);

//	gtk_widget_get_allocation (widget, &allocation);
//	surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24,
//                                     640,
//                                     500);
//	fbdata = cairo_image_surface_get_data(surface);
	// memset(fbdata, 0x00, 640*500*4);
//	draw_palette();

	/* We've handled the configure event, no need for further processing. */
	return TRUE;
}

// 256 colors (8bit), two hor pixel per color
// piture width = 640, center scale, i.e. start at 64
cairo_surface_t
*draw_palette(void)
{
unsigned int *p1, *pc;
int x,y;
static cairo_surface_t *ps=NULL;
cairo_t *cr;
unsigned char *fbdata;
char tdisp[16];

#define P_XPOS 175
#define P_YPOS 2
#define P_HEIGHT 14

	if (ps==NULL)
		ps=cairo_image_surface_create(CAIRO_FORMAT_RGB24, 640, 20);
	cr=cairo_create(ps);
	fbdata=cairo_image_surface_get_data(ps);
	memset(fbdata,0,(640*20*4));
	y=P_YPOS;
	for (x=0; x<256; x++) {
		fbdata[4* y * 640 + ((x+P_XPOS)*4)] = color_palette[3 * x + 2];  // B
		fbdata[(4* y * 640 + ((x+P_XPOS)*4))+1] = color_palette[3 * x + 1]; // G
		fbdata[(4* y * 640 + ((x+P_XPOS)*4))+2] = color_palette[3 * x]; // R
	}
	y=P_YPOS;
	p1 = (unsigned int *)&fbdata[4 * y * 640 + (P_XPOS*4)]; // pointer to start of line
	for (y=P_YPOS; y<(P_YPOS+P_HEIGHT); y++) {
		pc = (unsigned int *)&fbdata[4 * y * 640 + (P_XPOS*4)]; // pointer to start of copy line
		memcpy(pc,p1,256*4);
	}

	// update palette scale temperature range
	snprintf(tdisp, 16, "%.1f°C", t_min);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_select_font_face (cr, "Sans",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 18);
	cairo_move_to (cr, 102, 16);
	cairo_show_text (cr, tdisp);

	snprintf(tdisp, 16, "%.1f°C", t_max);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_select_font_face (cr, "Sans",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 18);
	cairo_move_to (cr, 440, 16);
	cairo_show_text (cr, tdisp);

	cairo_surface_flush(ps);
	cairo_destroy(cr);

	return ps;
}

void
store_vis_shot(unsigned char *jpg_buffer, unsigned int jpg_size)
{
time_t now;
struct tm *loctime;
char pname[PATH_MAX];
char fname[30];
int fd;

	now = time(NULL);
	loctime = localtime (&now);
	strftime (fname, 30, "viscam-%y%m%d%H%M%S", loctime);
	strncpy(pname, "./", PATH_MAX-30-4); // leave room for filename+extension
	strncat(pname, fname, PATH_MAX-5); // -5 to leave space for trailing \0 byte + extension
	strncat(pname, ".jpg", PATH_MAX-1); // -5 to leave space for trailing \0 byte + extension

	fd=open(pname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd>=0) {
		write (fd, jpg_buffer, jpg_size);
		close(fd);
	}
}


static gboolean
draw_event (GtkWidget *widget,
               cairo_t   *wcr,
               gpointer   data)
{
char tdisp[16];
cairo_surface_t *jpeg_surface;
cairo_surface_t *ir_surface;
cairo_surface_t *palette_surface;
cairo_t *cr;


	if (pending) {
		cr=cairo_create(psurface);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

		// first draw the frame buffer containing the IR frame
		if (ircam && ir_buffer!=NULL) {
			ir_surface=cairo_image_surface_create_for_data (ir_buffer,
	                                     CAIRO_FORMAT_RGB24,
	                                     160,
	                                     120,
	                                     4*160);
				cairo_save(cr);
				cairo_scale (cr, 4.0, 4.0);
				cairo_set_source_surface (cr, ir_surface, 0, 0);
				cairo_paint (cr);
				cairo_restore(cr);
				cairo_surface_destroy (ir_surface);
		}
		if (jpeg_size != 0 && jpeg_buffer != NULL) {
			if (take_vis_shot) {
				take_vis_shot=FALSE;
				store_vis_shot(jpeg_buffer, jpeg_size);
			}
			if (viscam) {
				jpeg_surface=cairo_image_surface_create_from_jpeg_mem(jpeg_buffer, jpeg_size);
				cairo_save(cr);
				cairo_scale (cr, (1./2.25), (1./2.25));
				cairo_set_source_surface (cr, jpeg_surface, 0, 0);
				if (ircam)
					cairo_paint_with_alpha (cr, vis_surface_alpha);
				else
					cairo_paint (cr);
				cairo_restore(cr);
				cairo_surface_destroy (jpeg_surface);
			}
			jpeg_size=0;
			jpeg_buffer=NULL;
		}

		// then draw decoration on top
		// the color palette with min/max temperatures
		palette_surface=draw_palette();
		cairo_save(cr);
		cairo_rectangle(cr,0,481,640,500);
		cairo_clip(cr);
		cairo_set_source_surface (cr, palette_surface, 0, 481);
		cairo_paint (cr);
		cairo_restore(cr);
	
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
		cairo_move_to (cr, 330, 220);
		cairo_show_text (cr, tdisp);
	
		// print battery % top right
		snprintf(tdisp, 16, "%d%%", battery_state.percentage);
		cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
		cairo_select_font_face (cr, "Sans",
			CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size (cr, 14);
		cairo_move_to (cr, 580, 20);
		cairo_show_text (cr, tdisp);

		cairo_destroy(cr);
		pending = FALSE;
	}
	cairo_set_source_surface (wcr, psurface, 0, 0);
	cairo_paint (wcr);

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

//
// store the current picture frame into a file
//
void
store_shot_clicked(GtkWidget *button, gpointer user_data)
{
time_t now;
struct tm *loctime;
char pname[PATH_MAX];
char fname[30];

	now = time(NULL);
	loctime = localtime (&now);
	strftime (fname, 30, "ircam-%y%m%d%H%M%S", loctime);
	strncpy(pname, "./", PATH_MAX-30-4); // leave room for filename+extension
	strncat(pname, fname, PATH_MAX-5); // -5 to leave space for trailing \0 byte + extension
	strncat(pname, ".png", PATH_MAX-1); // -1 to leave space for trailing \0 byte

	cairo_surface_write_to_png (psurface, pname);
	take_vis_shot=TRUE;
}

void
start_clicked(GtkWidget *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button))) {
		flir_run = TRUE;
		memset(&shutter_state, 0, sizeof(shutter_state));
		memset(&battery_state, 0, sizeof(battery_state));
		if (ir_buffer == NULL)
			g_printerr("ir_buffer\n");
		g_thread_new ("CAM thread", cam_thread_main, NULL);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(stop_button), FALSE);
	}
}

void
stop_clicked(GtkWidget *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(stop_button))) {
		flir_run = FALSE;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(play_button), FALSE);
	}
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
		// draw_palette();
	};
}


GtkWidget *
create_main_window (void)
{
//GtkWidget *gappw;
GtkWidget *box;
GtkWidget *hbox;
GtkWidget *w, *i;

// GtkWidget *da;

	// init default color palette
	color_palette = palette_Rainbow;

//		gappw=gtk_application_window_new(gapp);
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
	// w = gtk_button_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_DND);
	play_button = gtk_toggle_button_new();
	i = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_DND);
	gtk_button_set_image(GTK_BUTTON(play_button),i);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(play_button), FALSE);
	gtk_container_add (GTK_CONTAINER (hbox), play_button);
	g_signal_connect (play_button, "clicked",
		G_CALLBACK (start_clicked), NULL);

	// media-playback-stop
	// w = gtk_button_new_with_label("Stop");
	//w = gtk_button_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_DND);
	stop_button = gtk_toggle_button_new();
	i = gtk_image_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_DND);
	gtk_button_set_image(GTK_BUTTON(stop_button),i);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(stop_button), TRUE);
	gtk_container_add (GTK_CONTAINER (hbox), stop_button);

	g_signal_connect (stop_button, "clicked",
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

	// w = gtk_scale_new_with_range (GTK_ORIENTATION_VERTICAL, 0.0, 1.0, .01);

	psurface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, 640, 500);

	image_darea = gtk_drawing_area_new ();
	gtk_widget_set_size_request (image_darea, 640, 500);
	gtk_container_add (GTK_CONTAINER (box), image_darea);

	g_signal_connect (image_darea, "draw",
		G_CALLBACK (draw_event), NULL);
//	g_signal_connect (image_darea,"configure-event",
//		G_CALLBACK (configure_event), NULL);

	// camera-photo
	w = gtk_button_new_from_icon_name("camera-photo", GTK_ICON_SIZE_DND);
	gtk_container_add (GTK_CONTAINER (box), w);

	g_signal_connect (w, "clicked",
		G_CALLBACK (store_shot_clicked), NULL);

	gtk_widget_show_all(window);

	return window;
}

int
main(int argc, char **argv)
{
//	gapp=gtk_application_new("org.gnome.flirgtk", G_APPLICATION_FLAGS_NONE);
	gtk_init(&argc, &argv);

	create_main_window();

	gtk_main();

return 0;
}

