/* The Tuna Tuner UI
 *
 * Copyright (C) 2013 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define MTR_URI TUNA_URI
#define MTR_GUI "ui"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "src/tuna.h"

#ifndef MIN
#define MIN(A,B) ( (A) < (B) ? (A) : (B) )
#endif
#ifndef MAX
#define MAX(A,B) ( (A) > (B) ? (A) : (B) )
#endif

/* widget, window size */
#define DAWIDTH  (400.)
#define DAHEIGHT (300.)

typedef struct {
	LV2UI_Write_Function write;
	LV2UI_Controller controller;

	RobWidget *vbox;
	RobWidget *darea;

	PangoFontDescription *font[2];

	float p_mode;
	float p_tuning;
	float p_rms;
	float p_freq;
	float p_octave;
	float p_note;
	float p_cent;
	float p_error;

} TunaUI;

const char notename[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static bool expose_event(RobWidget* handle, cairo_t* cr, cairo_rectangle_t *ev)
{
	/* TODO: read from ringbuffer or blit cairo surface instead of [b]locking here */
	TunaUI* ui = (TunaUI*) GET_HANDLE(handle);

	/* limit cairo-drawing to exposed area */
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip(cr);

	CairoSetSouerceRGBA(c_blk);
	cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	cairo_rectangle (cr, 0, 0, DAWIDTH, DAHEIGHT);
	cairo_fill (cr);

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	char txt[255];

	write_text_full(cr, "The Tuna Tuner",
			ui->font[1], DAWIDTH/2, 30, 0, 2, c_wht);

	write_text_full(cr, "\u266B\u22D4\u266B",
			ui->font[0], DAWIDTH/2, 60, 0, 2, c_wht);

	snprintf(txt, 255,
			"+-------------------------+\n"
			"| Note: %-4s              |\n"
			"| Octave: %+2.0f              |\n"
			"| Cent: %+7.2f           |\n"
			"+-------------------------+\n"
			,
			notename[(int)ui->p_note],
			ui->p_octave,
			ui->p_cent);

	write_text_full(cr, txt,
			ui->font[0], DAWIDTH/2, DAHEIGHT/2, 0, 2, c_wht);

	cairo_rectangle (cr, 18, DAHEIGHT - 102, (DAWIDTH - 36) , 24);
	CairoSetSouerceRGBA(c_gry);
	cairo_fill(cr);

	if (ui->p_freq > 0) {
	snprintf(txt, 255, "%.1fHz", ui->p_freq );
		write_text_full(cr, txt,
				ui->font[0], DAWIDTH/2, DAHEIGHT-10, 0, 5, c_grn);

		if (fabsf(ui->p_cent) <= 5.0) {
			CairoSetSouerceRGBA(c_grn);
		} else {
			CairoSetSouerceRGBA(c_red);
		}
		cairo_rectangle (cr, DAWIDTH/2, DAHEIGHT - 100,
				(DAWIDTH/2 - 20) * ui->p_cent / 50.f, 20);
		cairo_fill(cr);

	} else {
		write_text_full(cr, " -- no signal -- ",
				ui->font[0], DAWIDTH/2, DAHEIGHT-10, 0, 5, c_red);
	}


	return TRUE;
}


static void ui_disable(LV2UI_Handle handle)
{
}

static void ui_enable(LV2UI_Handle handle)
{
}

/******************************************************************************
 * RobWidget
 */

static void
size_request(RobWidget* handle, int *w, int *h) {
	//TunaUI* ui = (TunaUI*)GET_HANDLE(handle);
	*w = DAWIDTH;
	*h = DAHEIGHT;
}

static RobWidget * toplevel(TunaUI* ui, void * const top)
{
	ui->vbox = rob_vbox_new(FALSE, 2);
	robwidget_make_toplevel(ui->vbox, top);
	ROBWIDGET_SETNAME(ui->vbox, "tuna");

	ui->darea = robwidget_new(ui);
	robwidget_set_alignment(ui->darea, 0, 0);
	robwidget_set_expose_event(ui->darea, expose_event);
	robwidget_set_size_request(ui->darea, size_request);
	rob_vbox_child_pack(ui->vbox, ui->darea, FALSE);

	ui->font[0] = pango_font_description_from_string("Mono 16");
	ui->font[1] = pango_font_description_from_string("Sans 12");
	return ui->vbox;
}

/******************************************************************************
 * LV2
 */

static LV2UI_Handle
instantiate(
		void* const               ui_toplevel,
		const LV2UI_Descriptor*   descriptor,
		const char*               plugin_uri,
		const char*               bundle_path,
		LV2UI_Write_Function      write_function,
		LV2UI_Controller          controller,
		RobWidget**               widget,
		const LV2_Feature* const* features)
{
	TunaUI* ui = (TunaUI*)calloc(1, sizeof(TunaUI));

	if (!ui) {
		fprintf(stderr, "Tuna.lv2 UI: out of memory\n");
		return NULL;
	}

	*widget = NULL;

	if (!strncmp(plugin_uri, MTR_URI "one", 31 + 3 )) {
		; //
	} else if (!strncmp(plugin_uri, MTR_URI "strobe", 31 + 6)) {
		; //
	} else {
		free(ui);
		return NULL;
	}

	/* initialize private data structure */
	ui->write      = write_function;
	ui->controller = controller;

	*widget = toplevel(ui, ui_toplevel);
	return ui;
}

static enum LVGLResize
plugin_scale_mode(LV2UI_Handle handle)
{
	return LVGL_LAYOUT_TO_FIT;
}

static void
cleanup(LV2UI_Handle handle)
{
	TunaUI* ui = (TunaUI*)handle;
	robwidget_destroy(ui->darea);
	rob_box_destroy(ui->vbox);
	pango_font_description_free(ui->font[0]);
	pango_font_description_free(ui->font[1]);
	free(ui);
}

static void
port_event(LV2UI_Handle handle,
		uint32_t     port_index,
		uint32_t     buffer_size,
		uint32_t     format,
		const void*  buffer)
{
	TunaUI* ui = (TunaUI*)handle;
	if (format != 0) return;
	const float v = *(float *)buffer;
	switch (port_index) {
		/* I/O ports */
		case TUNA_TUNING:
			ui->p_tuning = v; break;
		case TUNA_MODE:
			ui->p_mode = v;
			break;

		/* input ports */
		case TUNA_RMS:      ui->p_rms = v; break;
		case TUNA_FREQ_OUT: ui->p_freq = v; break;
		case TUNA_OCTAVE:   ui->p_octave = v; break;
		case TUNA_NOTE:     ui->p_note = MAX(0, MIN(11,v)); break;
		case TUNA_CENT:     ui->p_cent = v; break;
		case TUNA_ERROR:    ui->p_error = v; break;
		default:
												return;
	}
	queue_draw(ui->darea);
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}

/* vi:set ts=2 sts=2 sw=2: */
