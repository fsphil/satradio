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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rf.h"

#include <stdio.h>

/* RF sink interface */
extern double rf_scale(struct rf_t *s)
{
	if(s->scale != 0)
	{
		return(s->scale);
	}
	
	return(1.0);
}

extern int rf_live(struct rf_t *s)
{
	return(s->live);
}

int rf_write(struct rf_t *s, int16_t *iq_data, int samples)
{
	if(s->write)
	{
		return(s->write(s->private, iq_data, samples));
	}
	
	return(-1);
}

int rf_close(struct rf_t *s)
{
	if(s->close)
	{
		return(s->close(s->private));
	}
	
	return(0);
}

static double _hamming(double x)
{
	if(x < -1 || x > 1) return(0);
	return(0.54 - 0.46 * cos((M_PI * (1.0 + x))));
}

static double _rrc(double x, double b, double t)
{
	double r;
	
	/* Based on the Wikipedia page, https://en.wikipedia.org/w/index.php?title=Root-raised-cosine_filter&oldid=787851747 */
	
	if(x == 0)
	{
		r = (1.0 / t) * (1.0 + b * (4.0 / M_PI - 1));
	}
	else if(fabs(x) == t / (4.0 * b))
	{
		r = b / (t * sqrt(2.0)) * ((1.0 + 2.0 / M_PI) * sin(M_PI / (4.0 * b)) + (1.0 - 2.0 / M_PI) * cos(M_PI / (4.0 * b)));
	}
	else
	{
		double t1 = (4.0 * b * (x / t));
		double t2 = (sin(M_PI * (x / t) * (1.0 - b)) + 4.0 * b * (x / t) * cos(M_PI * (x / t) * (1.0 + b)));
		double t3 = (M_PI * (x / t) * (1.0 - t1 * t1));
		
		r = (1.0 / t) * (t2 / t3);
	}
	
	return(r);
}

void rf_qpsk_free(struct rf_qpsk_t *s)
{
	free(s->iwin);
	free(s->qwin);
	free(s->taps);
	memset(s, 0, sizeof(struct rf_qpsk_t));
}

int rf_qpsk_init(struct rf_qpsk_t *s, unsigned int interpolation, unsigned int decimation, double level)
{
	int i, j, x, n;
	unsigned int ntaps;
	double t;
	double *taps;
	
	memset(s, 0, sizeof(struct rf_qpsk_t));
	
	/* Generate the taps */
	ntaps = (5 * interpolation) | 1;
	taps = calloc(sizeof(double), ntaps);
	if(!taps)
	{
		return(-1);
	}
	
	n = ntaps / 2;
	for(x = 0; x < ntaps; x++)
	{
		t = ((double) x - n) / interpolation;
		taps[x] = _rrc(t, 0.5, 1.0) * M_SQRT1_2 * _hamming(((double) x - n) / n);
	}
	
	/* Configure and allocate memory */
	s->interpolation = interpolation;
	s->decimation = decimation;
	s->ntaps = ntaps + (ntaps % interpolation ? interpolation - (ntaps % interpolation) : 0);
	s->ataps = s->ntaps / interpolation;
	s->taps = calloc(sizeof(int16_t), s->ntaps);
	s->iwin = calloc(sizeof(int16_t) * 2, s->ataps);
	s->qwin = calloc(sizeof(int16_t) * 2, s->ataps);
	s->lwin = s->ataps;
	s->owin = 0;
	s->d = 0;
	s->sym = 0;
	
	if(!s->taps || !s->iwin || !s->qwin)
	{
		rf_qpsk_free(s);
		free(taps);
		return(-1);
	}
	
	/* Copy taps into the order they will be applied */
	j = s->ntaps - s->ataps;
	for(i = s->ntaps - 1; i >= 0; i--)
	{
		s->taps[j] = lround(taps[i] * INT16_MAX);
		j -= s->ataps;
		if(j < 0) j += s->ntaps + 1;
	}
	
	free(taps);
	
	return(0);
}

int rf_qpsk_process(struct rf_qpsk_t *s, int16_t *out, const uint8_t *src, int syms)
{
	const int16_t *taps;
	int16_t *iwin, *qwin;
	int32_t ai, aq;
	int x, y, z;
	int sym;
	
	for(z = x = 0; x < syms * 2; x += 2)
	{
		/* Read out the next 2-bit symbol, MSB first */
		sym = (src[x >> 3] >> (6 - (x & 0x07))) & 0x03;
		
		/* Append the next symbol to the round buffer */
		s->iwin[s->owin] = (sym & 2 ? 1 : -1);
		s->qwin[s->owin] = (sym & 1 ? 1 : -1);
		if(s->owin < s->ataps)
		{
			s->iwin[s->owin + s->lwin] = s->iwin[s->owin];
			s->qwin[s->owin + s->lwin] = s->qwin[s->owin];
		}
		if(++s->owin == s->lwin) s->owin = 0;
		
		for(; s->d < s->interpolation; s->d += s->decimation)
		{
			iwin = &s->iwin[s->owin];
			qwin = &s->qwin[s->owin];
			taps = &s->taps[s->d * s->ataps];
			
			/* Calculate the next output sample */
			for(ai = aq = y = 0; y < s->ataps; y++)
			{
				ai += iwin[y] * taps[y];
				aq += qwin[y] * taps[y];
			}
			
			*(out++) = ai < INT16_MIN ? INT16_MIN : (ai > INT16_MAX ? INT16_MAX : ai);
			*(out++) = aq < INT16_MIN ? INT16_MIN : (aq > INT16_MAX ? INT16_MAX : aq);
			z++;
		}
		s->d -= s->interpolation;
	}
	
	return(z);
}

void rf_fm_free(struct rf_fm_t *s)
{
	free(s->lut);
}

/* deviation = peak deviation in Hz (+/-) from frequency */
int rf_fm_init(struct rf_fm_t *s, unsigned int sample_rate, double frequency, double deviation, double level, int complex_out)
{
	int r;
	double d;
	
	memset(s, 0, sizeof(struct rf_fm_t));
	
	s->complex_out = complex_out ? 1 : 0;
	s->level = round(INT16_MAX * level);
	s->counter = INT16_MAX;
	s->phase[0] = INT32_MAX - INT16_MAX;
	s->phase[1] = 0;
	s->lut = malloc(sizeof(int32_t) * 2 * (UINT16_MAX + 1));
	if(!s->lut)
	{
		return(-1);
	}
	
	for(r = INT16_MIN; r <= INT16_MAX; r++)
	{
		d = 2.0 * M_PI / sample_rate * (frequency + (double) r / INT16_MAX * deviation);
		
		s->lut[(r - INT16_MIN) * 2 + 0] = lround(cos(d) * INT32_MAX);
		s->lut[(r - INT16_MIN) * 2 + 1] = lround(sin(d) * INT32_MAX);
	}
	
	return(0);
}

int rf_fm_process(struct rf_fm_t *s, int16_t *out, const int16_t *in, unsigned int samples)
{
	int64_t i, q;
	const int32_t *d;
	
	while(samples--)
	{
		d = &s->lut[(*(in++) - INT16_MIN) * 2];
		i = (((int64_t) s->phase[0] * (int64_t) d[0] - (int64_t) s->phase[1] * (int64_t) d[1]) + 0x3FFFFFFF) >> 31;
		q = (((int64_t) s->phase[0] * (int64_t) d[1] + (int64_t) s->phase[1] * (int64_t) d[0]) + 0x3FFFFFFF) >> 31;
		
		s->phase[0] = i;
		s->phase[1] = q;
		
		if(s->complex_out)
		{
			*(out++) = ((i >> 16) * s->level) >> 15;
			*(out++) = ((q >> 16) * s->level) >> 15;
		}
		else
		{
			*(out++) = ((i >> 16) * s->level) >> 15;
		}
		
		s->counter--;
	}
	
	/* Correct the amplitude after INT16_MAX samples */
	if(s->counter <= 0)
	{
		double ra = atan2(s->phase[1], s->phase[0]);
		
		s->phase[0] = lround(cos(ra) * (INT32_MAX - INT16_MAX));
		s->phase[1] = lround(sin(ra) * (INT32_MAX - INT16_MAX));
		
		s->counter = INT16_MAX;
	}
	
	return(0);
}

void rf_mixer_free(struct rf_mixer_t *s)
{
	/* Nothing to do */
}

int rf_mixer_init(struct rf_mixer_t *s, unsigned int sample_rate, double frequency, double level, int complex_out)
{
	double d;
	
	s->level = round(INT16_MAX * level);
	s->complex_out = complex_out ? 1 : 0;
	s->counter = INT16_MAX;
	
	s->phase[0] = INT32_MAX - INT16_MAX;
	s->phase[1] = 0;
	
	d = 2.0 * M_PI / sample_rate * frequency;
	s->delta[0] = lround(cos(d) * INT32_MAX);
	s->delta[1] = lround(sin(d) * INT32_MAX);
	
	return(0);
}

int rf_mixer_process(struct rf_mixer_t *s, int16_t *out, const int16_t *in, unsigned int samples)
{
	int64_t i, q;
	int32_t i2, q2;
	
	while(samples--)
	{
		/* Update the mixer signal */
		i = (((int64_t) s->phase[0] * (int64_t) s->delta[0] - (int64_t) s->phase[1] * (int64_t) s->delta[1]) + 0x3FFFFFFF) >> 31;
		q = (((int64_t) s->phase[0] * (int64_t) s->delta[1] + (int64_t) s->phase[1] * (int64_t) s->delta[0]) + 0x3FFFFFFF) >> 31;
		
		s->phase[0] = i;
		s->phase[1] = q;
		
		/* Adjust the level */
		i = ((i >> 16) * s->level) >> 15;
		q = ((q >> 16) * s->level) >> 15;
		
		/* Mix with the incoming signal */
		if(s->complex_out)
		{
			i2 = (((int32_t) in[0] * i - (int32_t) in[1] * q) + 0x3FFF) >> 15;
			q2 = (((int32_t) in[0] * q + (int32_t) in[1] * i) + 0x3FFF) >> 15;
			
			out[0] = i2;
			out[1] = q2;
			out += 2;
		}
		else
		{
			*(out++) = (((int32_t) in[0] * i - (int32_t) in[1] * q) + 0x3FFF) >> 15;
		}
		
		in += 2;
		s->counter--;
	}
	
	/* Correct the amplitude after INT16_MAX samples */
	if(s->counter <= 0)
	{
		double ra = atan2(s->phase[1], s->phase[0]);
		
		s->phase[0] = lround(cos(ra) * (INT32_MAX - INT16_MAX));
		s->phase[1] = lround(sin(ra) * (INT32_MAX - INT16_MAX));
		
		s->counter = INT16_MAX;
	}
	
	return(0);
}

int64_t rf_gcd(int64_t a, int64_t b)
{
	int64_t c;
	
	while((c = a % b))
	{
		a = b;
		b = c;
	}
	
	return(b);
}

