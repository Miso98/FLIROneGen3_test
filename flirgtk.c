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
static GtkApplication *gapp;
static GtkWidget *play_button, *stop_button;
 // we paint everything in here and then into the drawing area widget
static cairo_surface_t *psurface;
static gboolean take_vis_shot=FALSE;

// settings
static int colorpalette=9;

static gboolean show_battery=TRUE;
static gboolean show_palette=TRUE;
static gboolean show_crosshair=TRUE;
static gboolean shot_store_visual=FALSE;
static gboolean shot_store_ir_raw=FALSE;

static double vis_surface_alpha=0.3;
static double vis_surface_scaling=(1./2.25);
static double vis_x_offset=0.;
static double vis_y_offset=0.;


// variables to communicate with cam thread
gboolean pending=FALSE;
gboolean ircam=TRUE;
gboolean viscam=FALSE;
//gboolean flir_run = FALSE;
//unsigned char *color_palette;

gpointer cam_thread_main(gpointer user_data);

// data structure shared with camera thread
static struct t_data_t tdata;



gboolean
configure_event (GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
//GtkAllocation allocation;

	// g_printerr("configure event %d x %d\n", allocation.width, allocation.height);

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
		fbdata[4* y * 640 + ((x+P_XPOS)*4)] = tdata.color_palette[3 * x + 2];  // B
		fbdata[(4* y * 640 + ((x+P_XPOS)*4))+1] = tdata.color_palette[3 * x + 1]; // G
		fbdata[(4* y * 640 + ((x+P_XPOS)*4))+2] = tdata.color_palette[3 * x]; // R
	}
	y=P_YPOS;
	p1 = (unsigned int *)&fbdata[4 * y * 640 + (P_XPOS*4)]; // pointer to start of line
	for (y=P_YPOS; y<(P_YPOS+P_HEIGHT); y++) {
		pc = (unsigned int *)&fbdata[4 * y * 640 + (P_XPOS*4)]; // pointer to start of copy line
		memcpy(pc,p1,256*4);
	}

	// update palette scale temperature range
	snprintf(tdisp, 16, "%.1f°C", tdata.t_min);
	cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
	cairo_select_font_face (cr, "Sans",
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size (cr, 18);
	cairo_move_to (cr, 102, 16);
	cairo_show_text (cr, tdisp);

	snprintf(tdisp, 16, "%.1f°C", tdata.t_max);
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
const char *tmp;
char fname[30];
int fd;

	now = time(NULL);
	loctime = localtime (&now);
	strftime (fname, 30, "viscam-%y%m%d%H%M%S", loctime);

	tmp=g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	if (tmp == NULL)
		tmp = "./";
	strncpy(pname, tmp, PATH_MAX-30-4); // leave room for filename+extension
	strncat(pname, "/", PATH_MAX-5); // -5 to leave space for trailing \0 byte + extension
	strncat(pname, fname, PATH_MAX-5); // -5 to leave space for trailing \0 byte + extension
	strncat(pname, ".jpg", PATH_MAX-1); // -5 to leave space for trailing \0 byte + extension

	fd=open(pname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd>=0) {
		write (fd, jpg_buffer, jpg_size);
		close(fd);
	}
}


void
store_raw_ir_shot(void)
{
time_t now;
struct tm *loctime;
char pname[PATH_MAX];
const char *tmp;
char fname[30];
int fd;

	if (tdata.raw_ir_buffer == NULL) {
		g_printerr("raw IR buffer == NULL\n");
		return;
	}
	now = time(NULL);
	loctime = localtime (&now);
	strftime (fname, 30, "ir-%y%m%d%H%M%S", loctime);

	tmp=g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	if (tmp == NULL)
		tmp = "./";
	strncpy(pname, tmp, PATH_MAX-30-4);		// leave room for filename+extension
	strncat(pname, "/", PATH_MAX-5);		// -5 to leave space for trailing \0 byte + extension
	strncat(pname, fname, PATH_MAX-5);		// -5 to leave space for trailing \0 byte + extension
	strncat(pname, ".data", PATH_MAX-1);	// -5 to leave space for trailing \0 byte + extension

	fd=open(pname, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd>=0) {
		write (fd, (unsigned char *)tdata.raw_ir_buffer, 160*120*2);
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
		if (ircam && tdata.ir_buffer!=NULL) {
			ir_surface=cairo_image_surface_create_for_data (tdata.ir_buffer,
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

		if (tdata.jpeg_size != 0 && tdata.jpeg_buffer != NULL) {
			if (take_vis_shot) {
				take_vis_shot=FALSE;
				store_vis_shot(tdata.jpeg_buffer, tdata.jpeg_size);
			}
			if (viscam) {
				jpeg_surface=cairo_image_surface_create_from_jpeg_mem(tdata.jpeg_buffer, tdata.jpeg_size);
				cairo_save(cr);
				cairo_scale (cr, vis_surface_scaling, vis_surface_scaling);
				cairo_set_source_surface (cr, jpeg_surface, vis_x_offset, vis_y_offset);
				if (ircam)
					cairo_paint_with_alpha (cr, vis_surface_alpha);
				else
					cairo_paint (cr);
				cairo_restore(cr);
				cairo_surface_destroy (jpeg_surface);
			}
			tdata.jpeg_size=0;
			tdata.jpeg_buffer=NULL;
		}

		// then draw decoration on top
		// the color palette with min/max temperatures
		if (show_palette) {
			palette_surface=draw_palette();
			cairo_save(cr);
			cairo_rectangle(cr,0,481,640,500);
			cairo_clip(cr);
			cairo_set_source_surface (cr, palette_surface, 0, 481);
			cairo_paint (cr);
			cairo_restore(cr);
		}

		if (show_crosshair) {
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
			snprintf(tdisp, 16, "%.1f°C", tdata.t_center);
			cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
			cairo_select_font_face (cr, "Sans",
				CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size (cr, 24);
			cairo_move_to (cr, 330, 220);
			cairo_show_text (cr, tdisp);
		}

		// print battery % top right
		if (show_battery) {
			snprintf(tdisp, 16, "%d%%", tdata.battery_state.percentage);
			cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
			cairo_select_font_face (cr, "Sans",
				CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size (cr, 14);
			cairo_move_to (cr, 580, 20);
			cairo_show_text (cr, tdisp);
		}
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
const char *tmp;

	now = time(NULL);
	loctime = localtime (&now);
	strftime (fname, 30, "ircam-%y%m%d%H%M%S", loctime);

	tmp=g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	if (tmp == NULL)
		tmp = "./";
	strncpy(pname, tmp, PATH_MAX-30-4); // leave room for filename+extension
	strncat(pname, "/", PATH_MAX-5); // -5 to leave space for trailing \0 byte + extension
	strncat(pname, fname, PATH_MAX-5); // -5 to leave space for trailing \0 byte + extension
	strncat(pname, ".png", PATH_MAX-1); // -1 to leave space for trailing \0 byte
	cairo_surface_write_to_png (psurface, pname);

	if (shot_store_ir_raw)
		store_raw_ir_shot();

	if (shot_store_visual)
		take_vis_shot=TRUE;
}

void
start_clicked(GtkWidget *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(play_button))) {
		tdata.flir_run = TRUE;
		memset(&tdata.shutter_state, 0, sizeof(tdata.shutter_state));
		memset(&tdata.battery_state, 0, sizeof(tdata.battery_state));
		if (tdata.ir_buffer == NULL)
			g_printerr("ir_buffer\n");
		g_thread_new ("CAM thread", cam_thread_main, &tdata);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(stop_button), FALSE);
	}
}

void
stop_clicked(GtkWidget *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(stop_button))) {
		tdata.flir_run = FALSE;
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

void
menu_clicked(GtkWidget *button, gpointer user_data)
{

}

static void
close_window (void)
{
	// clean up and quit
	// g_application_quit(gapp);
	gtk_application_remove_window(gapp, GTK_WINDOW(window));
}

void
palette_changed (GtkComboBox *widget, gpointer user_data)
{
int act;

	act = gtk_combo_box_get_active(widget);
	if (act < 0) {
		g_printerr("oops, palette selection = %d\n", act);
	} else {
		if (act == 0) tdata.color_palette = palette_7;
		if (act == 1) tdata.color_palette = palette_15;
		if (act == 2) tdata.color_palette = palette_17;
		if (act == 3) tdata.color_palette = palette_85;
		if (act == 4) tdata.color_palette = palette_92;
		if (act == 5) tdata.color_palette = palette_Grayscale;
		if (act == 6) tdata.color_palette = palette_Grey;
		if (act == 7) tdata.color_palette = palette_Iron2;
		if (act == 8) tdata.color_palette = palette_Iron_Black;
		if (act == 9) tdata.color_palette = palette_Rainbow;
		colorpalette=act;
	};
}

void
emissivity_changed (GtkRange *range, gpointer user_data)
{
	tdata.emissivity=gtk_range_get_value (range);
}

void
tempreflected_changed (GtkSpinButton *spin_button, gpointer user_data)
{
	tdata.tempreflected=gtk_spin_button_get_value (spin_button);
}

void
ir_settings_activate(GSimpleAction *simple,
                       GVariant      *parameter,
                       gpointer       user_data)
{
GtkWidget *dialog, *hb, *c, *vb, *w;
GtkDialogFlags flags = GTK_DIALOG_USE_HEADER_BAR /*| GTK_DIALOG_MODAL*/ | GTK_DIALOG_DESTROY_WITH_PARENT;

	dialog = gtk_dialog_new_with_buttons ("IR Settings",
                                      GTK_WINDOW(window),
                                      flags,
                                      "_OK",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
	gtk_widget_set_size_request(dialog, 400, 200);
	hb=gtk_dialog_get_header_bar (GTK_DIALOG(dialog));
	gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR(hb), ":");

	c=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_add (GTK_CONTAINER (c), vb);

	w=gtk_label_new("Color Palette");
	gtk_container_add (GTK_CONTAINER (vb), w);

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
	gtk_combo_box_set_active (GTK_COMBO_BOX(w), colorpalette);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "changed",
		G_CALLBACK (palette_changed), NULL);

	w=gtk_label_new("Emissivity");
	gtk_container_add (GTK_CONTAINER (vb), w);

	w=gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                          0.0,
                          1.0,
                          0.01);
	gtk_scale_add_mark (GTK_SCALE(w), 0.30, GTK_POS_TOP, "shiny");
	gtk_scale_add_mark (GTK_SCALE(w), 0.60, GTK_POS_BOTTOM, "half shiny");
	gtk_scale_add_mark (GTK_SCALE(w), 0.80, GTK_POS_TOP, "half matte");
	gtk_scale_add_mark (GTK_SCALE(w), 0.90, GTK_POS_BOTTOM, "matte");
	gtk_range_set_value(GTK_RANGE(w), tdata.emissivity);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "value-changed",
		G_CALLBACK (emissivity_changed), NULL);

	w=gtk_label_new("Reflected Temperature");
	gtk_container_add (GTK_CONTAINER (vb), w);

	w=gtk_spin_button_new_with_range(-100.0, +100.0, 0.5);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), tdata.tempreflected);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "value-changed",
		G_CALLBACK (tempreflected_changed), NULL);

	gtk_widget_show_all(c);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}


void
show_battery_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	show_battery=gtk_toggle_button_get_active(togglebutton);
}

void
show_palette_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	show_palette=gtk_toggle_button_get_active(togglebutton);
}

void
show_crosshair_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	show_crosshair=gtk_toggle_button_get_active(togglebutton);
}

void
shot_visual_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	shot_store_visual=gtk_toggle_button_get_active(togglebutton);
}

void
shot_ir_raw_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	shot_store_ir_raw=gtk_toggle_button_get_active(togglebutton);
}

void
ui_settings_activate (GSimpleAction *simple,
                       GVariant      *parameter,
                       gpointer       user_data)
{
GtkWidget *dialog, *hb, *c, *vb, *w;
GtkDialogFlags flags = GTK_DIALOG_USE_HEADER_BAR /*| GTK_DIALOG_MODAL*/ | GTK_DIALOG_DESTROY_WITH_PARENT;

	dialog = gtk_dialog_new_with_buttons ("UI Settings",
                                      GTK_WINDOW(window),
                                      flags,
                                      "_OK",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
	gtk_widget_set_size_request(dialog, 400, 200);
	hb=gtk_dialog_get_header_bar (GTK_DIALOG(dialog));
	gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR(hb), ":");

	c=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_add (GTK_CONTAINER (c), vb);

	w=gtk_check_button_new_with_label ("Show battery");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), show_battery);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "toggled",
		G_CALLBACK (show_battery_toggled), NULL);
	
	w=gtk_check_button_new_with_label ("Show crosshair");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), show_crosshair);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "toggled",
		G_CALLBACK (show_crosshair_toggled), NULL);
	
	w=gtk_check_button_new_with_label ("Show palette");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), show_palette);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "toggled",
		G_CALLBACK (show_palette_toggled), NULL);
	
	w=gtk_check_button_new_with_label ("Shot store visual image");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), shot_store_visual);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "toggled",
		G_CALLBACK (shot_visual_toggled), NULL);
	
	w=gtk_check_button_new_with_label ("Shot store raw IR image");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), shot_store_ir_raw);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "toggled",
		G_CALLBACK (shot_ir_raw_toggled), NULL);
	
	gtk_widget_show_all(c);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}


void
vis_opacity_changed (GtkRange *range, gpointer user_data)
{
	vis_surface_alpha=gtk_range_get_value (range);
}

void
vis_scaling_changed (GtkRange *range, gpointer user_data)
{
	vis_surface_scaling=gtk_range_get_value (range);
}

void
vis_x_offset_changed (GtkSpinButton *spin_button, gpointer user_data)
{
	vis_x_offset=gtk_spin_button_get_value (spin_button);
}

void
vis_y_offset_changed (GtkSpinButton *spin_button, gpointer user_data)
{
	vis_y_offset=gtk_spin_button_get_value (spin_button);
}

void
vis_settings_activate
(GSimpleAction *simple,
                       GVariant      *parameter,
                       gpointer       user_data)
{
GtkWidget *dialog, *hb, *c, *vb, *w;
GtkDialogFlags flags = GTK_DIALOG_USE_HEADER_BAR /*| GTK_DIALOG_MODAL*/ | GTK_DIALOG_DESTROY_WITH_PARENT;

	dialog = gtk_dialog_new_with_buttons ("Vis Settings",
                                      GTK_WINDOW(window),
                                      flags,
                                      "_OK",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
	gtk_widget_set_size_request(dialog, 400, 200);
	hb=gtk_dialog_get_header_bar (GTK_DIALOG(dialog));
	gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR(hb), ":");

	c=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_add (GTK_CONTAINER (c), vb);

	w=gtk_label_new("Opacity");
	gtk_container_add (GTK_CONTAINER (vb), w);

	w=gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                          0.0,
                          1.0,
                          0.01);
	gtk_range_set_value(GTK_RANGE(w), vis_surface_alpha);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "value-changed",
		G_CALLBACK (vis_opacity_changed), NULL);

	w=gtk_label_new("Scaling");
	gtk_container_add (GTK_CONTAINER (vb), w);

	w=gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                          0.2,
                          1.0,
                          0.01);
	gtk_range_set_value(GTK_RANGE(w), vis_surface_scaling);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "value-changed",
		G_CALLBACK (vis_scaling_changed), NULL);

	w=gtk_label_new("X Offset");
	gtk_container_add (GTK_CONTAINER (vb), w);

	w=gtk_spin_button_new_with_range(-200.0, +200.0, 1.);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), vis_x_offset);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "value-changed",
		G_CALLBACK (vis_x_offset_changed), NULL);

	w=gtk_label_new("Y Offset");
	gtk_container_add (GTK_CONTAINER (vb), w);

	w=gtk_spin_button_new_with_range(-200.0, +200.0, 1.);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), vis_y_offset);
	gtk_container_add (GTK_CONTAINER (vb), w);
	g_signal_connect (w, "value-changed",
		G_CALLBACK (vis_y_offset_changed), NULL);



	gtk_widget_show_all(c);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}


void
quit_activate (GSimpleAction *simple, GVariant *parameter, gpointer user_data)
{
	close_window ();
}

void
create_main_window (GtkWidget *appwindow)
{
//GtkWidget *gappw;
GtkWidget *box;
GtkWidget *hbox;
GtkWidget *w, *i;
//GtkWidget *m, *mi;
GMenu *menu;
GActionGroup *group;
const GActionEntry entries[] = {
    { "uisettings", ui_settings_activate },
    { "irsettings", ir_settings_activate },
    { "vissettings", vis_settings_activate },
    { "quit", quit_activate },
  };

	// init default color palette
	tdata.color_palette = palette_Rainbow;

	window = appwindow;
	gtk_window_set_title (GTK_WINDOW (window), "FLIR One");
	w=gtk_header_bar_new();
	gtk_header_bar_set_show_close_button (GTK_HEADER_BAR(w), TRUE);
	gtk_header_bar_set_decoration_layout (GTK_HEADER_BAR(w), ":menu,close");
	i=gtk_menu_button_new();

	group = (GActionGroup*)g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (group), entries, G_N_ELEMENTS (entries), NULL);
	menu=g_menu_new();
	g_menu_append(menu, "UI Settings", "menu.uisettings");
	g_menu_append(menu, "IR Settings", "menu.irsettings");
	g_menu_append(menu, "Vis Settings", "menu.vissettings");
	g_menu_append(menu, "Quit", "menu.quit");

	gtk_widget_insert_action_group(i, "menu", group);
	gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(i), G_MENU_MODEL(menu));

	gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(i), TRUE);
	gtk_menu_button_set_direction(GTK_MENU_BUTTON(i), GTK_ARROW_NONE);


	gtk_header_bar_pack_end(GTK_HEADER_BAR(w),i);
	gtk_window_set_titlebar(GTK_WINDOW (window), w);

	g_signal_connect (window, "destroy",
		G_CALLBACK (close_window), NULL); 

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_container_add (GTK_CONTAINER (window), box);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_container_add (GTK_CONTAINER (box), hbox);

	// 48 GTK_ICON_SIZE_DIALOG
	// 32 GTK_ICON_SIZE_DND
	// media-playback-start
	play_button = gtk_toggle_button_new();
	i = gtk_image_new_from_icon_name("media-playback-start", GTK_ICON_SIZE_DND);
	gtk_button_set_image(GTK_BUTTON(play_button),i);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(play_button), FALSE);
	gtk_container_add (GTK_CONTAINER (hbox), play_button);
	g_signal_connect (play_button, "clicked",
		G_CALLBACK (start_clicked), NULL);

	stop_button = gtk_toggle_button_new();
	i = gtk_image_new_from_icon_name("media-playback-stop", GTK_ICON_SIZE_DND);
	gtk_button_set_image(GTK_BUTTON(stop_button),i);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(stop_button), TRUE);
	gtk_container_add (GTK_CONTAINER (hbox), stop_button);

	g_signal_connect (stop_button, "clicked",
		G_CALLBACK (stop_clicked), NULL);

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
}


void
flirgtk_app_activate (GApplication *application, gpointer user_data)
{
GtkWidget *widget;

	widget = gtk_application_window_new (GTK_APPLICATION (application));
	create_main_window(widget);
	gtk_window_present (GTK_WINDOW(widget));
}

int
main (int argc, char **argv)
{
	tdata.ir_buffer = (unsigned char *)malloc(640*480*4);
	tdata.raw_ir_buffer = (unsigned short *)malloc(169*120*2);
	tdata.emissivity=0.9;
	tdata.tempreflected=20.0;
	tdata.t_min=0.0;
	tdata.t_max=0.0;
	tdata.t_center=0.0;
	tdata.flir_run=FALSE;

	gapp=gtk_application_new("org.gnome.flirgtk", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(gapp, "activate", G_CALLBACK (flirgtk_app_activate), NULL);
	g_application_run (G_APPLICATION (gapp), argc, argv);
    g_object_unref (gapp);

return 0;
}

