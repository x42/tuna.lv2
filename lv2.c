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
#define RMS_SIGNAL_THRESHOLD     (0.000001f)
#define RMS_POSTFILTER_THRESHOLD (0.000003f)
#define FFT_PEAK_THESHOLD        (0.0000003f)

/* use FFT signal if tracked freq & FFT-freq differ by more than */
#define FFT_FREQ_THESHOLD_FAC (.05f)
/* but at least .. [Hz] */
#define FFT_FREQ_THESHOLD_MIN (5.f)


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

#include "spectr.c"
#include "fft.c"

static float fftx_scan_overtones(struct FFTAnalysis *ft, float fundamental, float *octave) {
	float freq = fundamental * (*octave);
	float scan  = MAX(1, freq * .1f);
	float peak_dat = 0;
	uint32_t peak_pos = 0;
	for (uint32_t i = MAX(1, floorf(freq-scan)); i < ceilf(freq+scan); ++i) {
		if (
				   ft->power[i] > FFT_PEAK_THESHOLD
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
		//printf("new fun %d %.2f @ %.1f\n", peak_pos, fundamental, (*octave));
		(*octave) *=2;
		if ((*octave) < 32) {
			fundamental = fftx_scan_overtones(ft, fundamental, octave);
		}
	}
	return fundamental;
}

static float fftx_find_note(struct FFTAnalysis *ft) {
	/* find lowest peak above threshold */
	float fundamental = 0;
	float octave = 0;
	float peak_dat = 0;
	const uint32_t brkpos = ft->data_size * 4000 / ft->rate;
	for (uint32_t i = 2; i < brkpos; ++i) {
		if (
				ft->power[i] > FFT_PEAK_THESHOLD
				&& ft->power[i] > ft->power[i-1]
				&& ft->power[i] > ft->power[i+1]
			 ) {
			float o = 2;
			float f = fftx_scan_overtones(ft, i, &o);
			if (o > 4) {
				if (ft->power[i] > peak_dat) {
					peak_dat = ft->power[i];
					fundamental = f;
					octave = o;
				}
			}
		}
	}

	debug_printf("fun: %.1f octave: %.0f freq: %.1fHz\n",
			fundamental, octave, ft->rate * fundamental / ft->data_size / 2.f);
	if (octave < 4) { return 0; }
	return ft->rate * fundamental / ft->data_size / 2.f;
}

/******************************************************************************
 * LV2 routines
 */

#define TUNA_URI "http://gareus.org/oss/lv2/tuna#one"

typedef enum {
	TUNA_AIN = 0,
	TUNA_AOUT,
	TUNA_TUNING,
	TUNA_RMS,
	TUNA_FREQ_OUT,
	TUNA_OCTAVE,
	TUNA_NOTE,
	TUNA_CENT,
	TUNA_ERROR,
} PortIndex;


typedef struct {
	/* LV2 ports */
	float* a_in;
	float* a_out;

	float* p_rms;
	float* p_tuning;
	float* p_freq_out;
	float* p_octave;
	float* p_note;
	float* p_cent;
	float* p_error;

	/* internal state */
	double rate;
	struct FilterBank fb;
	float tuna_fc; // center freq of expected note
	uint32_t filter_init;

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

	self->rate = rate;

	self->tuna_fc = 0;
	self->prev_smpl = 0;
	self->rms_signal = 0;
	self->rms_postfilter = 0;
	self->rms_omega = 1.0f - expf(-2.0 * M_PI * 20.0 / rate);
	self->dll_initialized = false;
	self->fft_initialized = false;
	self->fft_note = 0;
	self->fft_note_count = 0;

	self->fftx = (struct FFTAnalysis*) calloc(1, sizeof(struct FFTAnalysis));
	ft_init(self->fftx, MAX(8192, rate / 5), rate);

	return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle handle,
               uint32_t   port,
               void*      data)
{
	Tuna* self = (Tuna*)handle;

	switch ((PortIndex)port) {
		case TUNA_AIN:
			self->a_in  = (float*)data;
			break;
		case TUNA_AOUT:
			self->a_out = (float*)data;
			break;
		case TUNA_RMS:
			self->p_rms = (float*)data;
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
	}
}

static void
run(LV2_Handle handle, uint32_t n_samples)
{
	Tuna* self = (Tuna*)handle;

	/* input ports */
	float* a_in = self->a_in;
	const float tuning = *self->p_tuning;

	/* localize variables */
	float prev_smpl = self->prev_smpl;
	float rms_signal = self->rms_signal;
	float rms_postfilter = self->rms_postfilter;
	const float rms_omega  = self->rms_omega;

	/* initialize */
	float    detected_freq = 0;
	uint32_t detected_count = 0;

	bool fft_ran_this_cycle = 0 == fftx_run(self->fftx, n_samples, a_in);
	bool fft_proc_this_cycle = false;

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
			continue;
		}

		/* 2) detect frequency to track
		 * use FFT to rouhly detect the area
		 */
		float freq = self->tuna_fc;

		/* FFT accumulates data and only returns us some
		 * valid data once in a while.. */
		if (fft_ran_this_cycle && !fft_proc_this_cycle) {
			fft_proc_this_cycle = true;
			/* get lowest peak frequency */
			const float fft_peakfreq = fftx_find_note(self->fftx);
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

		/* refuse to track insanity */
		if (freq < 20 || freq > 10000 ) {
			self->dll_initialized = false;
			prev_smpl = 0;
			continue;
		}

		/* 2a) re-init detector coefficients with frequency to track */
		if (freq != self->tuna_fc) {
			self->tuna_fc = freq;
			info_printf("set filter: %.2fHz\n", freq);
			
			/* calculate DLL coefficients */
			const double omega = 4.0 * M_PI * self->tuna_fc / self->rate;
			self->dll_b = 1.4142135623730950488 * omega; // sqrt(2)
			self->dll_c = omega * omega;
			self->dll_initialized = false;

			/* re-initialize filter */
			bandpass_setup(&self->fb, self->rate, self->tuna_fc
					/* filter-bandwidth, a tad more than a semitone, but at least 15Hz */
					, MAX(5, self->tuna_fc * .15)
					, 4 /*th order butterworth */);
			self->filter_init = 16;
		}
		
		/* 3) band-pass filter the signal to clean up the
		 * waveform for counting zero-transitions.
		 */
		const float signal = bandpass_process(&self->fb, a_in[n]);

		if (self->filter_init > 0) {
			self->filter_init--;
			rms_postfilter = 0;
			continue;
		}

		/* 4) reject signals outside in the band */
		rms_postfilter += rms_omega * ( (signal * signal) - rms_postfilter) + 1e-20;
		if (rms_postfilter < RMS_POSTFILTER_THRESHOLD) {
			debug_printf("signal too low after filter: %f %f\n",
					20.*log10f(sqrt(rms_signal)),
					20.*log10f(sqrt(rms_postfilter)));
			self->dll_initialized = false;
			prev_smpl = 0;
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

				debug_printf("detected Freq: %.2f (error: %.2f [samples])\n",
						self->rate / (self->dll_t1 - self->dll_t0) / 2.f,
						self->dll_e0
						);

#if 1
				/* calculate average of all detected values in this cycle.
				 * this is questionable, just use last value.
				 */
				detected_freq += self->rate / (self->dll_t1 - self->dll_t0) / 2.f;
				detected_count++;
#else
				detected_freq = self->rate / (self->dll_t1 - self->dll_t0) / 2.f;
				detected_count= 1;
#endif
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
	  *self->p_error = -100.0;
	}
	/* else { no change, maybe short cycle } */

	/* report input level
	 * NB. 20 *log10f(sqrt(x)) == 10 * log(x) */
	*self->p_rms = (rms_signal > .0000000001f) ? 10. * log10f(rms_signal) : -100;
	//*self->p_rms = (rms_postfilter > .0000000001f) ? 10. * log10f(rms_postfilter) : -100;

	/* forward audio */
	if (self->a_in != self->a_out) {
		memcpy(self->a_out, self->a_in, sizeof(float) * n_samples);
	}
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

static const LV2_Descriptor descriptor = {
	TUNA_URI,
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor;
	default:
		return NULL;
	}
}

/* vi:set ts=2 sts=2 sw=2: */
