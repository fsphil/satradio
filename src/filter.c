/* satradio - Satellite radio sub-carrier modulator/encoder/transmitter  */
/*=======================================================================*/
/* Copyright 2022 Philip Heron <phil@sanslogic.co.uk>                    */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */

/* Taps for 40-15000Hz low pass filter at 32kHz with no, 50us and 75us
 * pre-emphasis. The phase response of these filters is not a good match for
 * a real FM pre-emphasis circuit, but audio quality seems unaffected */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "filter.h"

const double preemph_flat_taps[PREEMPH_TAPS] = {
	 0.000000,-0.000793, 0.000318,-0.001297, 0.000756,-0.002084, 0.001341,
	-0.003091, 0.001926,-0.004059, 0.002173,-0.004543, 0.001586,-0.003982,
	-0.000386,-0.001819,-0.004219, 0.002351,-0.010158, 0.008641,-0.018108,
	 0.016785,-0.027575, 0.026122,-0.037697, 0.035663,-0.047356, 0.044249,
	-0.055360, 0.050742,-0.060650, 0.054238, 0.937500, 0.054238,-0.060650,
	 0.050742,-0.055360, 0.044249,-0.047356, 0.035663,-0.037697, 0.026122,
	-0.027575, 0.016785,-0.018108, 0.008641,-0.010158, 0.002351,-0.004219,
	-0.001819,-0.000386,-0.003982, 0.001586,-0.004543, 0.002173,-0.004059,
	 0.001926,-0.003091, 0.001341,-0.002084, 0.000756,-0.001297, 0.000318,
	-0.000793,-0.000000
};

const double preemph_50us_taps[PREEMPH_TAPS] = {
	 0.001234,-0.002637, 0.002903,-0.004810, 0.005412,-0.008091, 0.008855,
	-0.012171, 0.012482,-0.015806, 0.014595,-0.016860, 0.012742,-0.012646,
	 0.004202,-0.000532,-0.013336, 0.021334,-0.041037, 0.053332,-0.078322,
	 0.093873,-0.122521, 0.139174,-0.168825, 0.183024,-0.210266, 0.214647,
	-0.236618, 0.196560,-0.226183,-0.606600, 2.497308,-0.606600,-0.226183,
	 0.196560,-0.236618, 0.214647,-0.210266, 0.183024,-0.168825, 0.139174,
	-0.122521, 0.093873,-0.078322, 0.053332,-0.041037, 0.021334,-0.013336,
	-0.000532, 0.004202,-0.012646, 0.012742,-0.016860, 0.014595,-0.015806,
	 0.012482,-0.012171, 0.008855,-0.008091, 0.005412,-0.004810, 0.002903,
	-0.002637, 0.001234
};

const double preemph_75us_taps[PREEMPH_TAPS] = {
	 0.001981,-0.003755, 0.004472,-0.006942, 0.008239,-0.011739, 0.013420,
	-0.017690, 0.018901,-0.022955, 0.022160,-0.024370, 0.019556,-0.017960,
	 0.007049, 0.000170,-0.018791, 0.032752,-0.059706, 0.080325,-0.114856,
	 0.140480,-0.180353, 0.207455,-0.249292, 0.271550,-0.312119, 0.315065,
	-0.356561, 0.275266,-0.363286,-0.992136, 3.546394,-0.992136,-0.363286,
	 0.275266,-0.356561, 0.315065,-0.312119, 0.271550,-0.249292, 0.207455,
	-0.180353, 0.140480,-0.114856, 0.080325,-0.059706, 0.032752,-0.018791,
	 0.000170, 0.007049,-0.017960, 0.019556,-0.024370, 0.022160,-0.022955,
	 0.018901,-0.017690, 0.013420,-0.011739, 0.008239,-0.006942, 0.004472,
	-0.003755, 0.001981
};

const double preemph_j17_taps[PREEMPH_TAPS] = {
	-0.000119,-0.000175,-0.000162,-0.000232,-0.000223,-0.000310,-0.000309,
	-0.000420,-0.000430,-0.000576,-0.000605,-0.000801,-0.000864,-0.001135,
	-0.001253,-0.001644,-0.001860,-0.002446,-0.002844,-0.003776,-0.004531,
	-0.006130,-0.007663,-0.010705,-0.014141,-0.020784,-0.029556,-0.046668,
	-0.072530,-0.124846,-0.211267,-0.400931, 2.279077,-0.400931,-0.211267,
	-0.124846,-0.072530,-0.046668,-0.029556,-0.020784,-0.014141,-0.010705,
	-0.007663,-0.006130,-0.004531,-0.003776,-0.002844,-0.002446,-0.001860,
	-0.001644,-0.001253,-0.001135,-0.000864,-0.000801,-0.000605,-0.000576,
	-0.000430,-0.000420,-0.000309,-0.000310,-0.000223,-0.000232,-0.000162,
	-0.000175,-0.000119
};

int fir_int32_init(struct fir_int32_t *s, const double *taps, unsigned int ntaps, int interpolation, int decimation, int delay)
{
	int i, j;
	
	s->type = 1;
	
	s->interpolation = interpolation;
	s->decimation = decimation;
	
	/* Round number of taps up to a multiple of the interpolation factor */
	s->ntaps = ntaps + (ntaps % interpolation ? interpolation - (ntaps % interpolation) : 0);
	s->ataps = s->ntaps / interpolation;
	
	s->itaps = malloc(s->ntaps * sizeof(int32_t));
	s->qtaps = NULL;
	
	j = s->ntaps - s->ataps;
	for(i = ntaps - 1; i >= 0; i--)
	{
		s->itaps[j] = lround(taps[i] * 32767.0);
		j -= s->ataps;
		if(j < 0) j += s->ntaps + 1;
	}
	
	s->lwin = s->ataps + delay;
	s->win = calloc(s->ataps * 2 + delay, sizeof(int32_t));
	s->owin = 0;
	s->d = 0;
	
	return(0);
}

size_t fir_int32_process(struct fir_int32_t *s, int32_t *out, const int32_t *in, size_t samples)
{
	int64_t a;
	int x, y;
	const int32_t *win, *taps;
	
	if(s->type == 0) return(0);
	//else if(s->type == 2) return(fir_int32_complex_process(s, out, in, samples));
	//else if(s->type == 3) return(fir_int32_scomplex_process(s, out, in, samples));
	
	for(x = 0; samples; samples--)
	{
		/* Append the next input sample to the round buffer */
		s->win[s->owin] = *in;
		if(s->owin < s->ataps) s->win[s->owin + s->lwin] = *in;
		if(++s->owin == s->lwin) s->owin = 0;
		
		for(; s->d < s->interpolation; s->d += s->decimation)
		{
			win = &s->win[s->owin];
			taps = &s->itaps[s->d * s->ataps];
			
			/* Calculate the next output sample */
			for(a = y = 0; y < s->ataps; y++)
			{
				a += (int64_t) *(win++) * (int64_t) *(taps++);
			}
			
			a >>= 15;
			*out = a < INT32_MIN ? INT32_MIN : (a > INT32_MAX ? INT32_MAX : a);
			out += 2;
			x++;
		}
		s->d -= s->interpolation;
		
		in += 2;
	}
	
	return(x);
}

void fir_int32_free(struct fir_int32_t *s)
{
	free(s->win);
	free(s->itaps);
	free(s->qtaps);
	memset(s, 0, sizeof(struct fir_int32_t));
}

void limiter_free(struct limiter_t *s)
{
	fir_int32_free(&s->vfir);
	fir_int32_free(&s->ffir);
	free(s->shape);
	free(s->att);
	free(s->fix);
	free(s->var);
}

int limiter_init(struct limiter_t *s, int16_t level, int width, const double *vtaps, const double *ftaps, int ntaps)
{
	int i;
	
	memset(s, 0, sizeof(struct limiter_t));
	
	if(ntaps > 0)
	{
		if(vtaps)
		{
			i = fir_int32_init(&s->vfir, vtaps, ntaps, 1, 1, 0);
			if(i != 0)
			{
				limiter_free(s);
				return(-1);
			}
		}
		
		if(ftaps)
		{
			i = fir_int32_init(&s->ffir, ftaps, ntaps, 1, 1, 0);
			if(i != 0)
			{
				limiter_free(s);
				return(-1);
			}
		}
	}
	
	/* Generate the limiter response shape */
	s->width = width | 1;
	s->shape = malloc(sizeof(int16_t) * s->width);
	if(!s->shape)
	{
		limiter_free(s);
		return(-1);
	}
	
	for(i = 0; i < s->width; i++)
	{
		s->shape[i] = lround((1.0 - cos(2.0 * M_PI / (s->width + 1) * (i + 1))) * 0.5 * INT16_MAX);
	}
	
	/* Initial state */
	s->level = level;
	s->att = calloc(sizeof(int16_t), s->width);
	s->fix = calloc(sizeof(int32_t), s->width);
	s->var = calloc(sizeof(int32_t), s->width);
	if(!s->att || !s->fix || !s->var)
	{
		limiter_free(s);
		return(-1);
	}
	
	s->p = 0;
	s->h = s->width / 2;
	
	return(0);
}

void limiter_process(struct limiter_t *s, int16_t *out, const int16_t *vin, const int16_t *fin, int samples, int step)
{
	int i, j;
	int32_t a, b;
	
	for(i = 0; i < samples; i++)
	{
		s->var[s->p] = *vin;
		s->fix[s->p] = (fin ? *fin : 0);
		s->att[s->p] = 0;
		
		/* Apply input filters */
		if(s->vfir.type) fir_int32_process(&s->vfir, &s->var[s->p], &s->var[s->p], 1);
		if(s->ffir.type) fir_int32_process(&s->ffir, &s->fix[s->p], &s->fix[s->p], 1);
		
		/* Hard limit the fixed input */
		if(s->fix[s->p] < -s->level) s->fix[s->p] = -s->level;
		else if(s->fix[s->p] > s->level) s->fix[s->p] = s->level;
		
		/* The variable signal is the difference between vin and fin */
		s->var[s->p] -= s->fix[s->p];
		
		if(++s->p == s->width) s->p = 0;
		if(++s->h == s->width) s->h = 0;
		
		/* Soft limit the variable input */
		a = abs(s->var[s->h] + s->fix[s->h]);
		if(a > s->level)
		{
			a = INT16_MAX - (s->level + abs(s->var[s->h]) - a) * INT16_MAX / abs(s->var[s->h]);
			
			for(j = 0; j < s->width; j++)
			{
				b = (a * s->shape[j]) >> 15;
				if(b > s->att[s->p]) s->att[s->p] = b;
				if(++s->p == s->width) s->p = 0;
			}
		}
		
		a  = s->fix[s->p];
		a += ((int64_t) s->var[s->p] * (INT16_MAX - s->att[s->p])) >> 15;
		
		/* Hard limit to catch rounding errors */
		if(a < -s->level) a = -s->level;
		else if(a > s->level) a = s->level;
		
		*out = a;
		
		vin += step;
		out += step;
		if(fin) fin += step;
	}
}

