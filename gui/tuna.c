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

/* pixel layout */
#define L_BAR_X (20)
#define L_BAR_W (DAWIDTH - 40)

#define L_CNT_YT (DAHEIGHT - 150)
#define L_CNT_XC (DAWIDTH / 2)
#define L_CNT_H (20.)

#define L_STB_H (20.)
#define L_STB_YC (DAHEIGHT - 105)

#define L_LVL_YT (DAHEIGHT - 80)
#define L_LVL_H  (10)

#define L_ERR_YT (DAHEIGHT - 60)
#define L_ERR_H  (10)

#define L_NFO_YC (80.)
#define L_NFO_XL (100.)
#define L_NFO_XC (325.)

#define L_TUN_XC (DAWIDTH / 2)
#define L_TUN_YC (125)


#define L_FOO_XC (DAWIDTH / 2)
#define L_FOO_YB (DAHEIGHT - 20)

typedef struct {
	LV2UI_Write_Function write;
	LV2UI_Controller controller;

	RobWidget *hbox, *ctable;
	RobWidget *darea;

	RobTkSep  *sep[3];
	RobTkLbl  *label[4];
	RobTkSpin *spb_tuning;
	RobTkSpin *spb_octave;
	RobTkSpin *spb_freq;
	RobTkSelect *sel_note;
	RobTkSelect *sel_mode;

	PangoFontDescription *font[4];
  cairo_surface_t *frontface;

	float p_rms;
	float p_freq;
	float p_octave;
	float p_note;
	float p_cent;
	float p_error;

	/* smoothed values for display */
	float s_rms;
	float s_error;

	float strobe_tme;
	float strobe_dpy;
	float strobe_phase;

	bool disable_signals;
} TunaUI;

static const char notename[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

enum fontsize {
	F_M_MED = 0,
	F_S_LARGE,
	F_M_HUGE,
	F_M_SMALL,
};


/******************************************************************************
 * Look & Feel
 */


static inline float log_meter (float db) {
	float def = 0.0f;

	if (db < -70.0f) {
		def = 0.0f;
	} else if (db < -60.0f) {
		def = (db + 70.0f) * 0.25f;
	} else if (db < -50.0f) {
		def = (db + 60.0f) * 0.5f + 2.5f;
	} else if (db < -40.0f) {
		def = (db + 50.0f) * 0.75f + 7.5f;
	} else if (db < -30.0f) {
		def = (db + 40.0f) * 1.5f + 15.0f;
	} else if (db < -20.0f) {
		def = (db + 30.0f) * 2.0f + 30.0f;
	} else if (db < 6.0f) {
		def = (db + 20.0f) * 2.5f + 50.0f;
	} else {
		def = 115.0f;
	}
	return (def/115.0f);
}

static int deflect(float val) {
	int lvl = rint(L_BAR_W * log_meter(val));
	if (lvl < 5) lvl = 5;
	if (lvl >= L_BAR_W) lvl = L_BAR_W;
	return lvl;
}

static void render_frontface(TunaUI* ui) {
  cairo_t *cr;
  if (!ui->frontface) {
    ui->frontface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, DAWIDTH, DAHEIGHT);
  }
  cr = cairo_create(ui->frontface);
	float c_bg[4];
	get_color_from_theme(1, c_bg);
	CairoSetSouerceRGBA(c_bg);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_rectangle (cr, 0, 0, DAWIDTH, DAHEIGHT);
  cairo_fill (cr);

	rounded_rectangle (cr, 10, 10, DAWIDTH - 20, DAHEIGHT - 20, 10);
  CairoSetSouerceRGBA(c_blk);
	cairo_fill(cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_line_width(cr, 1.0);

	write_text_full(cr, "The Tuna Tuner Tube",
			ui->font[F_S_LARGE], DAWIDTH/2, 30, 0, 2, c_wht);

	/* Cent scale */
	rounded_rectangle (cr, (L_CNT_XC - L_BAR_W/2) - 2, L_CNT_YT - 2, L_BAR_W + 4 , L_CNT_H + 4, 4);
	CairoSetSouerceRGBA(c_g30);
	cairo_fill(cr);
	write_text_full(cr, "C E N T", ui->font[F_S_LARGE], L_CNT_XC, L_CNT_YT + L_CNT_H/2, 0, 2, c_g60);

	for (int cent = -50; cent <= 50 ; cent +=10) {
		char tmp[16];
		snprintf(tmp, 16, "%+3d", cent);
		if (cent < 0) CairoSetSouerceRGBA(c_red);
		else if (cent > 0) CairoSetSouerceRGBA(c_grn);
		else CairoSetSouerceRGBA(c_glb);

		if (abs(cent) < 50) {
			const double dash[] = {2.0};
			cairo_save(cr);
			cairo_set_dash(cr, dash, 1, 0);
			cairo_move_to(cr, rintf(L_CNT_XC + L_BAR_W * cent / 100.) -.5, L_CNT_YT + 1.5);
			cairo_line_to(cr, rintf(L_CNT_XC + L_BAR_W * cent / 100.) -.5, L_CNT_YT + L_CNT_H - 1.5);
			cairo_stroke(cr);
			cairo_restore(cr);
		}

#if 0 // cents
		if (abs(cent) == 20 || abs(cent) == 40) {
			write_text_full(cr, tmp,
					ui->font[F_M_SMALL],
					rint(L_CNT_XC + L_BAR_W * cent / 100.)-.5, L_CNT_YT + L_CNT_H/2, 0 * M_PI, 2, c_g60);
		}
#endif
	}
	write_text_full(cr, "-50", ui->font[F_M_SMALL],
			rint(L_CNT_XC + L_BAR_W * -45 / 100.)-.5, L_CNT_YT + L_CNT_H/2, 0 * M_PI, 2, c_g60);
	write_text_full(cr, "+50", ui->font[F_M_SMALL],
			rint(L_CNT_XC + L_BAR_W *  45 / 100.)-.5, L_CNT_YT + L_CNT_H/2, 0 * M_PI, 2, c_g60);

	/* Strobe background */
	CairoSetSouerceRGBA(c_g30);
	rounded_rectangle (cr, L_BAR_X - 2, L_STB_YC - 12, L_BAR_W + 4 , L_STB_H + 4, 4);
	cairo_fill(cr);

	/* Level background */
	CairoSetSouerceRGBA(c_g30);
	rounded_rectangle (cr, L_BAR_X - 2, L_LVL_YT - 2, L_BAR_W + 4 , L_LVL_H + 4, 4);
	cairo_fill(cr);
	const double dash[] = {1.5};
	cairo_save(cr);
	cairo_set_dash(cr, dash, 1, 0);
	for (int db= -60; db <= 0; db+=6) {
		if (db < -18) {
			CairoSetSouerceRGBA(c_gry);
		} else if (db < 0) {
			CairoSetSouerceRGBA(c_g80);
		} else {
			CairoSetSouerceRGBA(c_red);
		}
		cairo_move_to(cr, rintf(L_BAR_X + deflect(db)) -.5, L_LVL_YT + 1.5);
		cairo_line_to(cr, rintf(L_BAR_X + deflect(db)) -.5, L_LVL_YT + L_LVL_H - 1.5);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
	write_text_full(cr, "signal level", ui->font[F_M_SMALL], L_CNT_XC, L_LVL_YT + L_LVL_H/2, 0, 2, c_g60);

	/* Accuracy background */
	CairoSetSouerceRGBA(c_g30);
	rounded_rectangle (cr, L_BAR_X - 2, L_ERR_YT - 2, L_BAR_W + 4 , L_ERR_H + 4, 4);
	cairo_fill(cr);
	write_text_full(cr, "accuracy", ui->font[F_M_SMALL], L_CNT_XC, L_ERR_YT + L_ERR_H/2, 0, 2, c_g60);

#if 1 /* version info */
  write_text_full(cr, "x42 tuna " TUNAVERSION, ui->font[F_M_SMALL],
      15, 20, 1.5 * M_PI, 7, c_g20);
#endif

  cairo_destroy(cr);
}



static bool expose_event(RobWidget* handle, cairo_t* cr, cairo_rectangle_t *ev)
{
	TunaUI* ui = (TunaUI*) GET_HANDLE(handle);

	/* limit cairo-drawing to exposed area */
	cairo_rectangle (cr, ev->x, ev->y, ev->width, ev->height);
	cairo_clip(cr);
  cairo_set_source_surface(cr, ui->frontface, 0, 0);
  cairo_paint (cr);

	cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
	char txt[255];
	const float tuning = robtk_spin_get_value(ui->spb_tuning);

	/* settings */
	snprintf(txt, 255, "@ %5.1fHz\n", tuning);
	write_text_full(cr, txt, ui->font[F_M_SMALL], L_TUN_XC, L_TUN_YC, 0, 1, c_wht);

	/* HUGE info: note, ocatave, cent */
	snprintf(txt, 255, "%-2s%.0f", notename[(int)ui->p_note], ui->p_octave);
	write_text_full(cr, txt, ui->font[F_M_HUGE], L_NFO_XL, L_NFO_YC, 0, 3, c_wht);

	if (ui->p_cent > -100) {
		snprintf(txt, 255, "%+6.2f\u00A2", ui->p_cent);
	} else {
		snprintf(txt, 255, "+-0.00\u00A2");
	}
	write_text_full(cr, txt, ui->font[F_M_MED], L_NFO_XC, L_NFO_YC, 0, 1, c_wht);

	float note = ui->p_note + (ui->p_octave+1) * 12.f;
	if (note >= 0 && note < 128) {
		const float note_freq = tuning * powf(2.0, (note - 69.f) / 12.f);
		snprintf(txt, 255, "%7.2fHz", note_freq);
		write_text_full(cr, txt, ui->font[F_M_MED], L_NFO_XC, L_TUN_YC, 0, 4, c_wht);
	}


	/* footer, Frequency || no-signal */
	if (ui->p_freq > 0) {
		snprintf(txt, 255, "%.2fHz", ui->p_freq );
		write_text_full(cr, txt,
				ui->font[F_M_MED], L_FOO_XC, L_FOO_YB, 0, 5, c_wht);
	} else {
		write_text_full(cr, " -- no signal -- ",
				ui->font[F_M_MED], L_FOO_XC, L_FOO_YB, 0, 5, c_g60);
	}

	/* cent bar graph */
	if (ui->p_freq > 0) {
		if (fabsf(ui->p_cent) <= 5.0) {
			cairo_set_source_rgba (cr, .0, .8, .0, .7);
		} else {
			cairo_set_source_rgba (cr, .8, .0, .0, .7);
		}
		cairo_rectangle (cr, L_CNT_XC, L_CNT_YT,
				L_BAR_W * ui->p_cent / 100., L_CNT_H);
		cairo_fill(cr);
	}

	/* level bar graph */
	if (ui->s_rms < -60) {
		cairo_set_source_rgba (cr, .0, .0, .8, .7);
	} else if (ui->s_rms < -10) {
		cairo_set_source_rgba (cr, .0, .8, .0, .7);
	} else if (ui->s_rms < -3) {
		cairo_set_source_rgba (cr, .8, .8, .0, .7);
	} else {
		cairo_set_source_rgba (cr, .8, .0, .0, .7);
	}
	rounded_rectangle (cr, L_BAR_X, L_LVL_YT, deflect(ui->s_rms), L_LVL_H, 4);
	cairo_fill(cr);

	/* accuracy bar graph */
	if (ui->p_freq == 0) {
		; // no data
	} else if (fabsf(ui->s_error) < 5) {
		cairo_set_source_rgba (cr, .0, .8, .0, .3);
		rounded_rectangle (cr, L_CNT_XC-50, L_ERR_YT, 100, L_ERR_H, 4);
		cairo_fill(cr);
	} else if (ui->s_error > -50 && ui->s_error < 50) {
		/* normal range -50..+50*/
		cairo_set_source_rgba (cr, .7, .8, .9, .7);
		cairo_rectangle (cr, L_CNT_XC, L_ERR_YT,
				L_BAR_W * ui->s_error / 150.0, L_ERR_H);
		cairo_fill(cr);
	} else if (ui->s_error > -100 && ui->s_error < 100) {
		/* out of phase */
		cairo_set_source_rgba (cr, .9, .5, .5, .7);
		cairo_rectangle (cr, L_CNT_XC, L_ERR_YT,
				L_BAR_W * ((ui->s_error + ((ui->s_error>0)?33.3:-33.3)) / 266.6), L_ERR_H);
		cairo_fill(cr);
	} else if (ui->s_error >= 100) {
		cairo_set_source_rgba (cr, .9, .0, .0, .7);
		cairo_rectangle (cr, L_CNT_XC, L_ERR_YT,
				L_BAR_W * 5 , L_ERR_H);
		cairo_fill(cr);
	} else if (ui->s_error <= -100) {
		cairo_set_source_rgba (cr, .9, .0, .0, .7);
		cairo_rectangle (cr, L_CNT_XC, L_ERR_YT,
				L_BAR_W * -.5 , L_ERR_H);
		cairo_fill(cr);
	}

	/* strobe setup */
	cairo_set_source_rgba (cr, .5, .5, .5, .8);
	if (ui->strobe_dpy != ui->strobe_tme) {
		if (ui->strobe_tme > ui->strobe_dpy) {
			float tdiff = ui->strobe_tme - ui->strobe_dpy;
			ui->strobe_phase += tdiff * ui->p_cent * 4;
			cairo_set_source_rgba (cr, .8, .8, .0, .8);
		}
		ui->strobe_dpy = ui->strobe_tme;
	}

	/* render strobe */
	cairo_save(cr);
	const double dash1[] = {8.0};
	const double dash2[] = {16.0};

	cairo_set_dash(cr, dash1, 1, ui->strobe_phase * -2.);
	cairo_set_line_width(cr, 8.0);
	cairo_move_to(cr, 20, L_STB_YC);
	cairo_line_to(cr, DAWIDTH-20, L_STB_YC);
	cairo_stroke (cr);

	cairo_set_dash(cr, dash2, 1, -ui->strobe_phase);
	cairo_set_line_width(cr, 16.0);
	cairo_move_to(cr, 20, L_STB_YC);
	cairo_line_to(cr, DAWIDTH-20, L_STB_YC);
	cairo_stroke (cr);
	cairo_restore(cr);


	return TRUE;
}

/******************************************************************************
 * UI callbacks
 */

static bool cb_set_mode (RobWidget* handle, void *data) {
	TunaUI* ui = (TunaUI*) (data);
	float mode = 0;
	switch(robtk_select_get_item(ui->sel_mode)) {
		default:
		case 0:
			robtk_select_set_sensitive(ui->sel_note, false);
			robtk_spin_set_sensitive(ui->spb_octave, false);
			robtk_spin_set_sensitive(ui->spb_freq,   false);
			break;
		case 1: /* freq */
			robtk_select_set_sensitive(ui->sel_note, false);
			robtk_spin_set_sensitive(ui->spb_octave, false);
			robtk_spin_set_sensitive(ui->spb_freq,   true);
			mode = robtk_spin_get_value(ui->spb_freq);
			break;
		case 2: /* note */
			robtk_select_set_sensitive(ui->sel_note, true);
			robtk_spin_set_sensitive(ui->spb_octave, true);
			robtk_spin_set_sensitive(ui->spb_freq,   false);
			mode = -1
				- rintf(robtk_spin_get_value(ui->spb_octave)+1) * 12.
				- robtk_select_get_value(ui->sel_note);
			break;
	}
	if (!ui->disable_signals) {
		ui->write(ui->controller, TUNA_MODE, sizeof(float), 0, (const void*) &mode);
	}
	return TRUE;
}

static bool cb_set_tuning (RobWidget* handle, void *data) {
	TunaUI* ui = (TunaUI*) (data);
	if (!ui->disable_signals) {
		float val = robtk_spin_get_value(ui->spb_tuning);
		ui->write(ui->controller, TUNA_TUNING, sizeof(float), 0, (const void*) &val);
	}
	return TRUE;
}

/******************************************************************************
 * RobWidget
 */


static void ui_disable(LV2UI_Handle handle)
{
}

static void ui_enable(LV2UI_Handle handle)
{
}

static void
size_request(RobWidget* handle, int *w, int *h) {
	//TunaUI* ui = (TunaUI*)GET_HANDLE(handle);
	*w = DAWIDTH;
	*h = DAHEIGHT;
}

static RobWidget * toplevel(TunaUI* ui, void * const top)
{
	ui->hbox = rob_hbox_new(FALSE, 2);
	robwidget_make_toplevel(ui->hbox, top);
	ROBWIDGET_SETNAME(ui->hbox, "tuna");

	ui->darea = robwidget_new(ui);
	robwidget_set_alignment(ui->darea, 0, 0);
	robwidget_set_expose_event(ui->darea, expose_event);
	robwidget_set_size_request(ui->darea, size_request);

  ui->ctable = rob_table_new(/*rows*/3, /*cols*/ 2, FALSE);

  ui->sep[0] = robtk_sep_new(TRUE);
  ui->sep[1] = robtk_sep_new(TRUE);
  ui->sep[2] = robtk_sep_new(TRUE);
  robtk_sep_set_linewidth(ui->sep[0], 0);
  robtk_sep_set_linewidth(ui->sep[1], 0);
  robtk_sep_set_linewidth(ui->sep[2], 1);

	ui->spb_tuning = robtk_spin_new(220, 880, .5);
	ui->spb_octave = robtk_spin_new(-1, 10, 1);
	ui->spb_freq   = robtk_spin_new(20, 1000, .5); // TODO log-map
	ui->sel_mode   = robtk_select_new();
	ui->sel_note   = robtk_select_new();

  robtk_select_add_item(ui->sel_mode,  0 , "Auto");
  robtk_select_add_item(ui->sel_mode,  1 , "Freq");
  robtk_select_add_item(ui->sel_mode,  2 , "Note");

  robtk_select_add_item(ui->sel_note,  0 , "C");
  robtk_select_add_item(ui->sel_note,  1 , "C#");
  robtk_select_add_item(ui->sel_note,  2 , "D");
  robtk_select_add_item(ui->sel_note,  3 , "D#");
  robtk_select_add_item(ui->sel_note,  4 , "E");
  robtk_select_add_item(ui->sel_note,  5 , "F");
  robtk_select_add_item(ui->sel_note,  6 , "F#");
  robtk_select_add_item(ui->sel_note,  7 , "G");
  robtk_select_add_item(ui->sel_note,  8 , "G#");
  robtk_select_add_item(ui->sel_note,  9 , "A");
  robtk_select_add_item(ui->sel_note, 10 , "A#");
  robtk_select_add_item(ui->sel_note, 11 , "B");

	ui->label[0] = robtk_lbl_new("Tuning");
	ui->label[1] = robtk_lbl_new("Octave");
	ui->label[2] = robtk_lbl_new("Note");
	ui->label[3] = robtk_lbl_new("Freq");

	/* default values */
  robtk_spin_set_default(ui->spb_tuning, 440);
  robtk_spin_set_value(ui->spb_tuning, 440);
  robtk_spin_set_default(ui->spb_freq, 440);
  robtk_spin_set_value(ui->spb_freq, 440);
  robtk_spin_set_default(ui->spb_octave, 4);
  robtk_spin_set_value(ui->spb_octave, 4);
  robtk_select_set_default_item(ui->sel_note, 9);
  robtk_select_set_item(ui->sel_note, 9);
	robtk_select_set_default_item(ui->sel_mode, 0);
	robtk_select_set_item(ui->sel_mode, 0);

	robtk_select_set_sensitive(ui->sel_note, false);
	robtk_spin_set_sensitive(ui->spb_octave, false);
	robtk_spin_set_sensitive(ui->spb_freq,   false);

	/* layout alignments */
  robtk_spin_set_alignment(ui->spb_octave, 0, 0.5);
  robtk_spin_label_width(ui->spb_octave, -1, 20);
  robtk_spin_set_label_pos(ui->spb_octave, 2);
  robtk_spin_set_alignment(ui->spb_tuning, 1.0, 0.5);
  robtk_spin_label_width(ui->spb_tuning, -1, 0);
  robtk_spin_set_label_pos(ui->spb_tuning, 2);
  robtk_spin_set_alignment(ui->spb_freq, 0, 0.5);
  robtk_spin_label_width(ui->spb_freq, -1, 0);
  robtk_spin_set_label_pos(ui->spb_freq, 2);
  robtk_select_set_alignment(ui->sel_note, 0, .5);
	robtk_lbl_set_alignment(ui->label[1], 0, .5);
	robtk_lbl_set_alignment(ui->label[2], 0, .5);
	robtk_lbl_set_alignment(ui->label[3], 0, .5);

  /* table layout */
  int row = 0;
#define TBLADD(WIDGET, X0, X1, Y0, Y1) \
  rob_table_attach(ui->ctable, WIDGET, X0, X1, Y0, Y1, 2, 2, RTK_SHRINK, RTK_SHRINK);

  rob_table_attach(ui->ctable, robtk_sep_widget(ui->sep[0])
			, 0, 2, row, row+1, 2, 2, RTK_SHRINK, RTK_EXANDF);
	row++;

	TBLADD(robtk_lbl_widget(ui->label[0]), 0, 2, row, row+1);
	row++;
  TBLADD(robtk_spin_widget(ui->spb_tuning), 0, 2, row, row+1);
	row++;

  rob_table_attach(ui->ctable, robtk_sep_widget(ui->sep[1])
			, 0, 2, row, row+1, 2, 2, RTK_SHRINK, RTK_EXANDF);
	row++;

  rob_table_attach(ui->ctable, robtk_select_widget(ui->sel_mode)
			, 0, 2, row, row+1, 2, 2, RTK_EXANDF, RTK_SHRINK);
	row++;

	TBLADD(robtk_lbl_widget(ui->label[2]), 0, 1, row, row+1);
  TBLADD(robtk_select_widget(ui->sel_note), 1, 2, row, row+1);
  rob_table_attach(ui->ctable, robtk_select_widget(ui->sel_note)
			, 1, 2, row, row+1, 2, 2, RTK_EXANDF, RTK_SHRINK);
	row++;

	TBLADD(robtk_lbl_widget(ui->label[1]), 0, 1, row, row+1);
  TBLADD(robtk_spin_widget(ui->spb_octave), 1, 2, row, row+1);
	row++;

  rob_table_attach(ui->ctable, robtk_sep_widget(ui->sep[2])
			, 1, 2, row, row+1, 2, 2, RTK_EXANDF, RTK_SHRINK);
	row++;
	TBLADD(robtk_lbl_widget(ui->label[3]), 0, 1, row, row+1);
  TBLADD(robtk_spin_widget(ui->spb_freq), 1, 2, row, row+1);

	/* global layout */
	rob_hbox_child_pack(ui->hbox, ui->darea, FALSE);
  rob_hbox_child_pack(ui->hbox, ui->ctable, FALSE);

	/* signal callbacks */
	robtk_select_set_callback(ui->sel_mode, cb_set_mode, ui);
	robtk_select_set_callback(ui->sel_note, cb_set_mode, ui);
	robtk_spin_set_callback(ui->spb_freq, cb_set_mode, ui);
	robtk_spin_set_callback(ui->spb_octave, cb_set_mode, ui);
	robtk_spin_set_callback(ui->spb_tuning, cb_set_tuning, ui);

	/* misc */
	ui->font[0] = pango_font_description_from_string("Mono 14");
	ui->font[1] = pango_font_description_from_string("Sans 12");
	ui->font[2] = pango_font_description_from_string("Mono 48");
	ui->font[3] = pango_font_description_from_string("Mono 8");
	return ui->hbox;
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

	ui->strobe_dpy = 0;
	ui->strobe_tme = 0;
	ui->strobe_phase = 0;
	ui->s_rms = 0;
	ui->s_error = 0;

	*widget = NULL;

	if (!strncmp(plugin_uri, TUNA_URI "one", 31 + 3 )) {
		; //
	} else if (!strncmp(plugin_uri, TUNA_URI "strobe", 31 + 6)) {
		; //
	} else {
		free(ui);
		return NULL;
	}

	/* initialize private data structure */
	ui->write      = write_function;
	ui->controller = controller;

	*widget = toplevel(ui, ui_toplevel);
	render_frontface(ui);
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

	for (uint32_t i = 0; i < 2; ++i) {
		robtk_sep_destroy(ui->sep[i]);
	}
	for (uint32_t i = 0; i < 4; ++i) {
		robtk_lbl_destroy(ui->label[i]);
	}

  robtk_spin_destroy(ui->spb_tuning);
  robtk_spin_destroy(ui->spb_octave);
  robtk_spin_destroy(ui->spb_freq);
  robtk_select_destroy(ui->sel_note);
	robtk_select_destroy(ui->sel_mode);

	rob_box_destroy(ui->hbox);

  cairo_surface_destroy(ui->frontface);
	for (uint32_t i = 0; i < 4; ++i) {
		pango_font_description_free(ui->font[i]);
	}
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
			ui->disable_signals = true;
			robtk_spin_set_value(ui->spb_tuning, v);
			ui->disable_signals = false;
			break;
		case TUNA_MODE:
			ui->disable_signals = true;
			if (v > 0 && v <= 10000) {
				robtk_select_set_item(ui->sel_mode, 1);
				robtk_spin_set_value(ui->spb_freq, v);
			} else if (v <= -1 && v >= -128) {
				robtk_select_set_item(ui->sel_mode, 2);
				int note = -1 - v;
				robtk_spin_set_value(ui->spb_octave, (note / 12) -1);
				robtk_select_set_item(ui->sel_note, note % 12);
			} else {
				robtk_select_set_item(ui->sel_mode, 0);
			}
			ui->disable_signals = false;
			break;

		/* input ports */
		case TUNA_FREQ_OUT: ui->p_freq = v; break;
		case TUNA_OCTAVE:   ui->p_octave = v; break;
		case TUNA_NOTE:     ui->p_note = MAX(0, MIN(11,v)); break;
		case TUNA_CENT:     ui->p_cent = v; break;
		case TUNA_RMS:      ui->p_rms = v;
			if (ui->p_rms < -70) {
				ui->s_rms = -70;
			} else {
				ui->s_rms += .3 * (v - ui->s_rms) + 1e-12;
			}
			break;

		case TUNA_ERROR:    ui->p_error = v;
			if (ui->p_error==0) {
				ui->s_error = 0;
			} else {
				ui->s_error += .03 * (v - ui->s_error) + 1e-12;
			}
			break;

		case TUNA_STROBE:
			ui->strobe_tme = v;
			queue_draw(ui->darea);
			break;
		default:
			return;
	}
}

	static const void*
extension_data(const char* uri)
{
	return NULL;
}

/* vi:set ts=2 sts=2 sw=2: */
