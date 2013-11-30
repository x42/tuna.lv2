/* FFT analysis - spectrogram
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

#include <stdio.h>
#include <sys/types.h>
#include <fftw3.h>

/*****************************************************************************
 * internal FFT abstraction
 */
struct FFTAnalysis {
	uint32_t window_size;
	uint32_t data_size;
	double rate;
	float *hann_window;
	float *fft_in;
	float *fft_out;
	float *power;
	fftwf_plan fftplan;

	float *ringbuf;
	uint32_t rboff;
	uint32_t afpvf;
};

static void ft_init(struct FFTAnalysis *ft, uint32_t window_size, double rate) {
	ft->rate        = rate;
	ft->window_size = window_size;
	ft->data_size   = window_size / 2;
	ft->hann_window = NULL;
	ft->rboff = 0;
	ft->afpvf = 0;

	ft->ringbuf = (float *) calloc(window_size, sizeof(float));
	ft->fft_in  = (float *) fftwf_malloc(sizeof(float) * window_size);
	ft->fft_out = (float *) fftwf_malloc(sizeof(float) * window_size);
	ft->power   = (float *) calloc(ft->data_size, sizeof(float));

	memset(ft->fft_out, 0, sizeof(float) * window_size);
	ft->fftplan = fftwf_plan_r2r_1d(window_size, ft->fft_in, ft->fft_out, FFTW_R2HC, FFTW_ESTIMATE);
}

static void ft_free(struct FFTAnalysis *ft) {
	fftwf_destroy_plan(ft->fftplan);
	free(ft->hann_window);
	free(ft->ringbuf);
	free(ft->fft_in);
	free(ft->fft_out);
	free(ft->power);
	free(ft);
}

static float * ft_hann_window(struct FFTAnalysis *ft) {
	if (ft->hann_window) return ft->hann_window;
	ft->hann_window = (float *) malloc(sizeof(float) * ft->window_size);
	double sum = 0.0;

	for (uint32_t i=0; i < ft->window_size; i++) {
		ft->hann_window[i] = 0.5f - (0.5f * (float) cos(2.0f * M_PI * (float)i / (float)(ft->window_size)));
		sum += ft->hann_window[i];
	}
	const double isum = 2.0 / sum;
	for (uint32_t i=0; i < ft->window_size; i++) {
		ft->hann_window[i] *= isum;
	}

	return ft->hann_window;
}

static void ft_analyze(struct FFTAnalysis *ft) {
	float *window = ft_hann_window(ft);
	for (uint32_t i = 0; i < ft->window_size; i++) {
		ft->fft_in[i] *= window[i];
	}

	fftwf_execute(ft->fftplan);
	ft->power[0] = ft->fft_out[0] * ft->fft_out[0];

#define FRe (ft->fft_out[i])
#define FIm (ft->fft_out[ft->window_size-i])
	for (uint32_t i = 1; i < ft->data_size - 1; ++i) {
		ft->power[i] = (FRe * FRe) + (FIm * FIm);
	}
#undef FRe
#undef FIm
}

static void fftx_reset(struct FFTAnalysis *ft) {
	memset(ft->power, 0, ft->data_size * sizeof(float));
	memset(ft->ringbuf, 0, ft->window_size * sizeof(float));
	memset(ft->fft_out, 0, ft->window_size * sizeof(float));
	ft->rboff = 0;
	ft->afpvf = 0;
}

static int fftx_run(struct FFTAnalysis *ft,
		const uint32_t n_samples, float const * const data)
{
	assert(n_samples <= ft->window_size);

	float * const f_buf = ft->fft_in;
	float * const r_buf = ft->ringbuf;

	const uint32_t n_off = ft->rboff;
	const uint32_t n_siz = ft->window_size;
	const uint32_t n_old = n_siz - n_samples;

	/* copy new data into ringbuffer and fft-buffer
	 * TODO: use memcpy
	 */
	for (uint32_t i = 0; i < n_samples; ++i) {
		r_buf[ (i + n_off) % n_siz ]  = data[i];
		f_buf[n_old + i] = data[i];
	}

	ft->rboff = (ft->rboff + n_samples) % n_siz;
	/* process at most at 30Hz */
	ft->afpvf += n_samples;
	if (ft->afpvf < ft->rate / 30) {
		return -1;
	}
	ft->afpvf = 0;

	/* copy samples from ringbuffer into fft-buffer */
	const uint32_t p0s = (n_off + n_samples) % n_siz;
	if (p0s + n_old >= n_siz) {
		const uint32_t n_p1 = n_siz - p0s;
		const uint32_t n_p2 = n_old - n_p1;
		memcpy(f_buf, &r_buf[p0s], sizeof(float) * n_p1);
		memcpy(&f_buf[n_p1], &r_buf[0], sizeof(float) * n_p2);
	} else {
		memcpy(&f_buf[0], &r_buf[p0s], sizeof(float) * n_old);
	}

	/* ..and analyze */
	ft_analyze(ft);
	return 0;
}
