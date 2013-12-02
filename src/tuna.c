/* Tuner.lv2
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <stdbool.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"

#ifndef MIN
#define MIN(A,B) ( (A) < (B) ? (A) : (B) )
#endif
#ifndef MAX
#define MAX(A,B) ( (A) > (B) ? (A) : (B) )
#endif


/* NB. RMS calculation does not take sqrt, for optimization
 * we compare the threshold against the squared signal.
 *
 * -40dBFS = (10^(.05 * -40))^2 = 0.0001
 * -45dBFS = (10^(.05 * -45))^2 = 0.000031623
 * -50dBFS = (10^(.05 * -50))^2 = 0.00001
 * -60dBFS = (10^(.05 * -60))^2 = 0.000001
 * -65dBFS = (10^(.05 * -65))^2 = 0.000000316
 * -70dBFS = (10^(.05 * -70))^2 = 0.0000001
 * -90dBFS = (10^(.05 * -70))^2 = 0.000000001
 */
#define RMS_SIGNAL_THRESHOLD      (0.0000001f)

/* use FFT signal if tracked freq & FFT-freq differ by more than */
#define FFT_FREQ_THESHOLD_FAC (.05f)
/* but at least .. [Hz] */
#define FFT_FREQ_THESHOLD_MIN (5.f)

/* for testing only -- output filtered signal */
//#define OUTPUT_POSTFILTER

/* debug */
#if 0
#define info_printf printf
#else
void info_printf (const char *fmt,...) {}
#endif

#if 0 // lots of output
#define debug_printf printf
#else
void debug_printf (const char *fmt,...) {}
#endif


/*****************************************************************************/

#include "tuna.h"
#include "spectr.c"
#include "fft.c"

static float fftx_scan_overtones(struct FFTAnalysis *ft,
		const float threshold, float fundamental, float *octave, float *fq) {
	float freq = fundamental * (*octave);
	float scan  = MAX(2, freq * .1f);
	float peak_dat = 0;
	uint32_t peak_pos = 0;
	const float freq_per_bin = ft->rate / ft->data_size / 2.f;
	const float phasediff = M_PI * ft->step / ft->data_size;
	for (uint32_t i = MAX(1, floorf(freq-scan)); i < ceilf(freq+scan); ++i) {
		if (
				   ft->power[i] > threshold
				&& ft->power[i] > peak_dat
				&& ft->power[i] > ft->power[i-1]
				&& ft->power[i] > ft->power[i+1]
			 ) {
			peak_pos = i;
			peak_dat = ft->power[i];
			break;
		}
	}
	if (peak_pos > 0) {
		fundamental = (float) peak_pos / (*octave);
		const uint32_t i = peak_pos;
		float phase = ft->phase[i] - ft->phase_h[i] - (float) i * phasediff;

		/* clamp to -M_PI .. M_PI */
		int over = phase / M_PI;
		over += (over >= 0) ? (over&1) : -(over&1);
		phase -= M_PI*(float)over;
		/* scale according to overlap */
		phase *= (ft->data_size / ft->step) / M_PI;
		(*fq) = freq_per_bin * ((float) i + phase) / (*octave);
		(*octave) *=2;
		if ((*octave) < 32) {
			fundamental = fftx_scan_overtones(ft, threshold, fundamental, octave, fq);
		}
	}
	return fundamental;
}

static float fftx_find_note(struct FFTAnalysis *ft, float threshold) {
	/* find lowest peak above threshold */
	float fundamental = 0;
	float freq = 0;
	float octave = 0;
	float peak_dat = 0;
	const uint32_t brkpos = ft->data_size * 4000 / ft->rate;
	const uint32_t baspos = ft->data_size * 100 / ft->rate;

	for (uint32_t i = 2; i < brkpos; ++i) {
		if (
				ft->power[i] > threshold
				&& ft->power[i] > ft->power[i-1]
				&& ft->power[i] > ft->power[i+1]
			 ) {

			float o = 2;
			float fq = 0;
			float f = fftx_scan_overtones(ft, threshold, i, &o, &fq);
			if (o > 3 || (i > baspos && o > 2)) {
				if (ft->power[i] > peak_dat) {
					peak_dat = ft->power[i];
					fundamental = f;
					freq = fq;
					octave = o;
					/* TODO -- this needs some more thought..
					 * only prefer higher 'fundamental' if it's louder than
					 * a /usual/ 1st overtone.
					 */
					threshold = peak_dat * 20; // ~26dB
					//break;
				}
			}
		}
	}

	debug_printf("fun: %.1f octave: %.0f freq: %.1fHz freq: %.2fHz %d\n",
			fundamental, octave, ft->rate * fundamental / ft->data_size / 2.f, freq, baspos);
	if (octave < 4) { return 0; }
	return freq;
}

/******************************************************************************
 * LV2 routines
 */

typedef struct {
	/* LV2 ports */
	float* a_in;
	float* a_out;

	float* p_mode;
	float* p_rms;
	float* p_tuning;
	float* p_freq_out;
	float* p_octave;
	float* p_note;
	float* p_cent;
	float* p_error;
	float* p_strobe;

	/* internal state */
	double rate;
	struct FilterBank fb;
	float tuna_fc; // center freq of expected note
	uint32_t filter_init;
	bool initialize;

	/* discriminator */
	float prev_smpl;

	/* RMS / threshold */
	float rms_omega;
	float rms_signal;
	float rms_postfilter;

	/* DLL */
	bool dll_initialized;
	uint32_t monotonic_cnt;
	double dll_e2, dll_e0;
	double dll_t0, dll_t1;
	double dll_b, dll_c;

	/* FFT */
	struct FFTAnalysis *fftx;
	bool fft_initialized;
	int fft_note;
	int fft_note_count;
	bool fftonly_variant;

	/* MIDI Out */
	bool midi_variant;
  LV2_URID_Map* map;
  LV2_Atom_Forge forge;
  LV2_Atom_Forge_Frame frame;
  LV2_Atom_Sequence* midiout;
	uint8_t m_key, m_vel;

	uint8_t m_stat_key;
	uint32_t m_stat_cnt;

	LV2_URID atom_Sequence;
	LV2_URID uri_midi_Event;
} Tuna;

static LV2_Handle
instantiate(
		const LV2_Descriptor*     descriptor,
		double                    rate,
		const char*               bundle_path,
		const LV2_Feature* const* features)
{
	Tuna* self = (Tuna*)calloc(1, sizeof(Tuna));
	if(!self) {
		return NULL;
	}

	self->fftonly_variant = false;
	if (!strncmp(descriptor->URI, TUNA_URI "one", 31 + 3 )) {
		self->midi_variant = false;
	} else if (!strncmp(descriptor->URI, TUNA_URI "fft", 31 + 3 )) {
		self->midi_variant = false;
		self->fftonly_variant = true;
	} else if (!strncmp(descriptor->URI, TUNA_URI "midi", 31 + 4 )) {
		int i;
		for (i=0; features[i]; ++i) {
			if (!strcmp(features[i]->URI, LV2_URID__map)) {
				self->map = (LV2_URID_Map*)features[i]->data;
			}
		}
		if (!self->map) {
			fprintf(stderr, "tuna.lv2 error: Host does not support urid:map\n");
			free(self);
			return NULL;
		}
		self->uri_midi_Event = self->map->map(self->map->handle, LV2_MIDI__MidiEvent);
		self->atom_Sequence  = self->map->map(self->map->handle, LV2_ATOM__Sequence);
		lv2_atom_forge_init(&self->forge, self->map);
		self->midi_variant = true;
	} else {
		return NULL;
	}

	self->rate = rate;

	self->tuna_fc = 0;
	self->prev_smpl = 0;
	self->rms_signal = 0;
	self->rms_postfilter = 0;
	self->rms_omega = 1.0f - expf(-2.0 * M_PI * 15.0 / rate);
	self->dll_initialized = false;
	self->fft_initialized = false;
	self->fft_note = 0;
	self->fft_note_count = 0;
	self->initialize = !self->midi_variant;

	self->dll_e0 = self->dll_e2 = 0;
	self->dll_t1 = self->dll_t0 = 0;

	self->fftx = (struct FFTAnalysis*) calloc(1, sizeof(struct FFTAnalysis));
	int fft_size;
	if (self->fftonly_variant || self->midi_variant) {
		fft_size = MAX(8192, rate / 5);
	} else {
		fft_size = MAX(2048, rate / 20);
	}
	/* round up to next power of two */
	fft_size--;
	fft_size |= fft_size >> 1;
	fft_size |= fft_size >> 2;
	fft_size |= fft_size >> 4;
	fft_size |= fft_size >> 8;
	fft_size |= fft_size >> 16;
	fft_size++;
	ft_init(self->fftx, fft_size, rate, 60);

	return (LV2_Handle)self;
}

static void
connect_port_tuna(
		LV2_Handle handle,
		uint32_t   port,
		void*      data)
{
	Tuna* self = (Tuna*)handle;

	switch ((PortIndexTuna)port) {
		case TUNA_AIN:
			self->a_in  = (float*)data;
			break;
		case TUNA_AOUT:
			self->a_out = (float*)data;
			break;
		case TUNA_RMS:
			self->p_rms = (float*)data;
			break;
		case TUNA_MODE:
			self->p_mode = (float*)data;
			break;
		case TUNA_TUNING:
			self->p_tuning = (float*)data;
			break;
		case TUNA_FREQ_OUT:
			self->p_freq_out = (float*)data;
			break;
		case TUNA_OCTAVE:
			self->p_octave = (float*)data;
			break;
		case TUNA_NOTE:
			self->p_note = (float*)data;
			break;
		case TUNA_CENT:
			self->p_cent = (float*)data;
			break;
		case TUNA_ERROR:
			self->p_error = (float*)data;
			break;
		case TUNA_STROBE:
			self->p_strobe = (float*)data;
			break;
	}
}

static void
connect_port_midi(
		LV2_Handle handle,
		uint32_t   port,
		void*      data)
{
	Tuna* self = (Tuna*)handle;

	switch ((PortIndexMidi)port) {
		case MIDI_AIN:
			self->a_in  = (float*)data;
			break;
		case MIDI_MOUT:
			self->midiout = (LV2_Atom_Sequence*)data;
			break;
		case MIDI_TUNING:
			self->p_tuning = (float*)data;
			break;
	}
}

static void midi_tx(Tuna *self, int64_t tme, uint8_t raw_midi[3]) {
	LV2_Atom midiatom;
	midiatom.type = self->uri_midi_Event;
	midiatom.size = 3;
  lv2_atom_forge_frame_time(&self->forge, tme);
	lv2_atom_forge_raw(&self->forge, &midiatom, sizeof(LV2_Atom));
	lv2_atom_forge_raw(&self->forge, raw_midi, 3);
	lv2_atom_forge_pad(&self->forge, sizeof(LV2_Atom) + midiatom.size);
}

static void midi_signal(Tuna *self, uint32_t tme, float freq, float rms) {
	uint8_t raw_midi[3];

	if (freq < 10 || rms == 0) {
		if (self->m_vel == 0 || self->m_key == 0) return;

		// ignore the first detection
		if (self->m_stat_key != 255) {
			self->m_stat_key = 255;
			self->m_stat_cnt = 1;
			return;
		}
		// and a few others too.
		if (self->m_stat_cnt < 100) {
			self->m_stat_cnt++;
			return;
		}
		raw_midi[0] = 0x80;
		raw_midi[1] = self->m_key;
		raw_midi[2] = self->m_vel = 0;
		self->m_stat_key = self->m_stat_cnt = 0;
		self->m_key = 0;
	} else {
		const float tuning = *self->p_tuning;
		const int key = rintf(12.f * logf(freq / tuning) / logf(2.f) + 69.0);
		const int vel = 127;

		// ignore the first detection
		if (self->m_stat_key != key) {
			self->m_stat_key = key;
			self->m_stat_cnt = 1;
			return;
		}
		// and a few others too.
		if (self->m_stat_cnt < 16) {
			self->m_stat_cnt++;
			return;
		}

		// TODO handle velocity (and velocity changes ?)
		if (self->m_vel == vel && self->m_key == key) return;
		if (self->m_vel != 0 && self->m_key != key) {
			/* send note off */
			raw_midi[0] = 0x80;
			raw_midi[1] = self->m_key;
			raw_midi[2] = 0;
			midi_tx(self, tme, raw_midi);
		}

		raw_midi[0] = 0x90;
		raw_midi[1] = self->m_key = key&127;
		raw_midi[2] = self->m_vel = vel&127;
	}
	midi_tx(self, tme, raw_midi);
}



static void
run(LV2_Handle handle, uint32_t n_samples)
{
	Tuna* self = (Tuna*)handle;

	/* first time around.
	 *
	 * this plugin does not always set ports every run()
	 * so we better initialize them.
	 *
	 * (liblilv does it according to .ttl,too * but better safe than sorry)
	 * */
	if (self->initialize) {
		self->initialize  = false;
		*self->p_freq_out = 0;
		*self->p_octave   = 4;
		*self->p_note     = 9;
		*self->p_cent     = 0;
		*self->p_error    = -100;
	}
	if (self->midi_variant) {
		const uint32_t capacity = self->midiout->atom.size;
		lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->midiout, capacity);
		lv2_atom_forge_sequence_head(&self->forge, &self->frame, 0);
	}

	/* input ports */
	float const * const a_in = self->a_in;
	const float  tuning = *self->p_tuning;
	const float  mode   = self->midi_variant ? 0 : *self->p_mode;
#ifdef OUTPUT_POSTFILTER
	float * const a_out = self->midi_variant ? NULL : self->a_out;
#endif

	/* localize variables */
	float prev_smpl = self->prev_smpl;
	float rms_signal = self->rms_signal;
	float rms_postfilter = self->rms_postfilter;
	const float rms_omega  = self->rms_omega;
	float freq = self->tuna_fc;

	/* initialize local vars */
	float    detected_freq = 0;
	uint32_t detected_count = 0;
	bool fft_ran_this_cycle = false;
	bool fft_proc_this_cycle = false;

	/* operation mode */
	if (mode > 0 && mode < 10000) {
		/* fixed user-specified frequency */
		freq = mode;
	} else if (mode <= -1 && mode >= -128) {
		/* midi-note */
		freq = tuning * powf(2.0, floorf(-70 - mode) / 12.f);
	} else {
		/* auto-detect */
		fft_ran_this_cycle = 0 == fftx_run(self->fftx, n_samples, a_in);
	}

	/* process every sample */
	for (uint32_t n = 0; n < n_samples; ++n) {

		/* 1) calculate RMS */
		rms_signal += rms_omega * ((a_in[n] * a_in[n]) - rms_signal) + 1e-20;

		if (rms_signal < RMS_SIGNAL_THRESHOLD) {
			/* signal below threshold */
			self->dll_initialized = false;
			self->fft_initialized = false;
			self->fft_note_count = 0;
			prev_smpl = 0;
#ifdef OUTPUT_POSTFILTER
			if (!self->midi_variant) { a_out[n] = 0; }
#endif
			midi_signal(self, n, 0, 0);
			continue;
		}

		/* 2) detect frequency to track
		 * use FFT to roughly detect the area
		 */

		/* FFT accumulates data and only returns us some
		 * valid data once in a while.. */
		if (fft_ran_this_cycle && !fft_proc_this_cycle) {
			fft_proc_this_cycle = true;
			/* get lowest peak frequency */
			const float fft_peakfreq = fftx_find_note(self->fftx, MAX(RMS_SIGNAL_THRESHOLD, rms_signal * .001)); // needs tweaking
			if (fft_peakfreq < 20) {
				self->fft_note_count = 0;
			} else {
				/* calculate corresponding note - use midi notation 0..127 */
				const int note = rintf(12.f * logf(fft_peakfreq / tuning) / logf(2.f) + 69.0);
				/* ..and round it back to frequency */
				const float note_freq = tuning * powf(2.0, (note - 69.f) / 12.f);

				/* keep track of fft stability */
				if (note == self->fft_note) {
					self->fft_note_count+=n_samples;
				} else {
					self->fft_note_count = 0;
				}
				self->fft_note = note;

				debug_printf("FFT found peak: %fHz -> midi: %d -> freq: %fHz (%d)\n", fft_peakfreq, note, note_freq, self->fft_note_count);

				if (self->fftonly_variant) {
					if (self->fft_note_count > 1) {
						detected_freq = fft_peakfreq;
						detected_count= 1;
						self->dll_initialized = true; // fake for readout
					}
				} else
				if ((note >=0 && note < 128) && freq != note_freq &&
						(   !self->dll_initialized
						 || freq < 20
						 /* ignore 1st overtone (often louder than fundamental) */
						 || (self->fft_note_count > 8192 && fabsf(freq * 2.f - note_freq) > 10)
						 || (self->fft_note_count > 2048 && fabsf(freq - note_freq) > MAX(FFT_FREQ_THESHOLD_MIN, freq * FFT_FREQ_THESHOLD_FAC))
						 || (self->fft_note_count > self->rate)
						)
					 ) {
					info_printf("FFT adjust %fHz -> %fHz (midi: %d)\n", freq, note_freq, note);
					freq = note_freq;
				}
			}
		}

		if (self->fftonly_variant) {
			// cont and calc RMS (used for threshold)
			continue;
		}

		/* refuse to track insanity */
		if (freq < 20 || freq > 10000 ) {
			self->dll_initialized = false;
			prev_smpl = 0;
#ifdef OUTPUT_POSTFILTER
			if (!self->midi_variant) { a_out[n] = 0; }
#endif
			midi_signal(self, n, 0, 0);
			continue;
		}

		/* 2a) re-init detector coefficients with frequency to track */
		if (freq != self->tuna_fc) {
			self->tuna_fc = freq;
			info_printf("set filter: %.2fHz\n", freq);
			
			/* calculate DLL coefficients */
			const double omega = ((self->tuna_fc < 50) ? 6.0 : 4.0) * M_PI * self->tuna_fc / self->rate;
			self->dll_b = 1.4142135623730950488 * omega; // sqrt(2)
			self->dll_c = omega * omega;
			self->dll_initialized = false;

			/* re-initialize filter */
			bandpass_setup(&self->fb, self->rate, self->tuna_fc
					/* filter-bandwidth, a tad more than a semitone, but at least 15Hz */
					, MAX(8, self->tuna_fc * .15)
					, 2 /*th order butterworth */);
			self->filter_init = 16;
		}
		
		/* 3) band-pass filter the signal to clean up the
		 * waveform for counting zero-transitions.
		 */
		const float signal = bandpass_process(&self->fb, a_in[n]);

		if (self->filter_init > 0) {
			self->filter_init--;
			rms_postfilter = 0;
#ifdef OUTPUT_POSTFILTER
			if (!self->midi_variant) {
				a_out[n] = signal * (16.0 - self->filter_init) / 16.0;
			}
#endif
			midi_signal(self, n, 0, 0);
			continue;
		}
#ifdef OUTPUT_POSTFILTER
		if (!self->midi_variant) {
			a_out[n] = signal;
		}
#endif

		/* 4) reject signals outside in the band */
		rms_postfilter += rms_omega * ( (signal * signal) - rms_postfilter) + 1e-20;
		if (rms_postfilter < rms_signal * ((self->tuna_fc < 50) ? .01 : .02)) {
			debug_printf("signal too low after filter: %f %f\n",
					20.*log10f(sqrt(rms_signal)),
					20.*log10f(sqrt(rms_postfilter)));
			self->dll_initialized = false;
			prev_smpl = 0;
			midi_signal(self, n, 0, 0);
			continue;
		}

		/* 5) track phase by counting
		 * rising-edge zero-transitions
		 * and a 2nd order phase-locked loop
		 */
		if (   (signal >= 0 && prev_smpl < 0)
				|| (signal <= 0 && prev_smpl > 0)
				) {

			if (!self->dll_initialized) {
				info_printf("reinit DLL\n");
				/* re-initialize DLL */
				self->dll_initialized = true;
				self->dll_e0 = self->dll_t0 = 0;
				self->dll_e2 = self->rate / self->tuna_fc / 2.f;
				self->dll_t1 = self->monotonic_cnt + n + self->dll_e2;
			} else {
				/* phase 'error' = detected_phase - expected_phase */
				self->dll_e0 = (self->monotonic_cnt + n) - self->dll_t1;

				/* update DLL, keep track of phase */
				self->dll_t0 = self->dll_t1;
				self->dll_t1 += self->dll_b * self->dll_e0 + self->dll_e2;
				self->dll_e2 += self->dll_c * self->dll_e0;

				const float dfreq = self->rate / (self->dll_t1 - self->dll_t0) / 2.f;
				debug_printf("detected Freq: %.2f (error: %.2f [samples])\n",
						dfreq, self->dll_e0);

#if 1
				/* calculate average of all detected values in this cycle.
				 * this is questionable, just use last value.
				 */
				detected_freq += dfreq;
				detected_count++;
#else
				detected_freq = dfreq;
				detected_count= 1;
#endif
				midi_signal(self, n, dfreq, rms_signal);
			}
		}
		prev_smpl = signal;
	}

	/* copy back variables */
	self->prev_smpl = prev_smpl;
	self->rms_signal = rms_signal;
	self->rms_postfilter = rms_postfilter;

	if (!self->dll_initialized) {
		self->monotonic_cnt = 0;
	} else {
		self->monotonic_cnt += n_samples;
	}

	if (self->midi_variant) {
		//lv2_atom_forge_pop(&self->forge, &self->frame);
		return;
	}

	/* post-processing and data-output */
	if (detected_count > 0) {
		/* calculate average of detected frequency */
		const float freq_avg = detected_freq / (float)detected_count;
		/* ..and the corresponding note */
		const int note = rintf(12.f * logf(freq_avg / tuning) / logf(2.f) + 69.0);
		const float note_freq = tuning * powf(2.0, (note - 69.f) / 12.f);

		debug_printf("detected Freq: %.2f (error: %.2f [samples])\n", freq_avg, self->dll_e0);

		/* calculate cent difference */
		float cent;
		if (freq_avg < note_freq) {
			/* cent -100..0 use 2^(−1÷12) = 0.943874313
			 * cent = (freq_avg - note_freq) / (note_freq - note_freq * 0.943874313f);
			 */
			cent = (freq_avg / note_freq - 1.f) / (1.f - 0.943874313f);
		} else {
			/* cent  0..100 use 2^(+1÷12) = 1.059463094
			 * cent = (freq_avg - note_freq) / (note_freq * 1.059463094f - note_freq);
			 */
			cent = (freq_avg / note_freq - 1.f) / (1.059463094f - 1.f);
		}

		/* assign output port data */
	  *self->p_freq_out = freq_avg;
	  *self->p_octave   = (note/12) -1;
	  *self->p_note     = note%12;
	  *self->p_cent     = 100.f * cent;
	  *self->p_error    =  50.0 * self->dll_e0 / note_freq;

	}
	else if (!self->dll_initialized) {
		/* no signal detected; or below threshold */
		*self->p_freq_out = 0;
	  *self->p_error = 0;
	}
	/* else { no change, maybe short cycle } */

	/* report input level
	 * NB. 20 *log10f(sqrt(x)) == 10 * log(x) */
	*self->p_rms = (rms_signal > .0000000001f) ? 10. * log10f(rms_signal) : -100;
	//*self->p_rms = (rms_postfilter > .0000000001f) ? 10. * log10f(rms_postfilter) : -100;

	*self->p_strobe = self->monotonic_cnt / self->rate; // kick UI

#ifndef OUTPUT_POSTFILTER
	/* forward audio */
	if (self->a_in != self->a_out) {
		memcpy(self->a_out, self->a_in, sizeof(float) * n_samples);
	}
#endif
}

static void
cleanup(LV2_Handle handle)
{
	Tuna* self = (Tuna*)handle;
	ft_free(self->fftx);
	free(handle);
}


/******************************************************************************
 * LV2 setup
 */

const void*
extension_data(const char* uri)
{
	return NULL;
}

#define mkdesc_tuna(ID, NAME) \
static const LV2_Descriptor descriptor ## ID = { \
	TUNA_URI NAME,     \
	instantiate,       \
	connect_port_tuna, \
	NULL,              \
	run,               \
	NULL,              \
	cleanup,           \
	extension_data     \
};

#define mkdesc_midi(ID, NAME) \
static const LV2_Descriptor descriptor ## ID = { \
	TUNA_URI NAME,     \
	instantiate,       \
	connect_port_midi, \
	NULL,              \
	run,               \
	NULL,              \
	cleanup,           \
	extension_data     \
};

mkdesc_tuna(0, "one")
mkdesc_tuna(1, "one_gtk")
mkdesc_midi(2, "midi")

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch (index) {
    case  0: return &descriptor0;
    case  1: return &descriptor1;
    case  2: return &descriptor2;
    default: return NULL;
	}
}

/* vi:set ts=2 sts=2 sw=2: */