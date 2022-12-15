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

#ifndef _FILTER_H
#define _FILTER_H

#include <stdint.h>

/* Pre-defined audio filter taps */

#define PREEMPH_TAPS 65

extern const double preemph_flat_taps[PREEMPH_TAPS];
extern const double preemph_50us_taps[PREEMPH_TAPS];
extern const double preemph_75us_taps[PREEMPH_TAPS];
extern const double preemph_j17_taps[PREEMPH_TAPS];

/* FIR filters (int32) */

struct fir_int32_t {
	
	int type;
	
	int interpolation;
	int decimation;
	
	unsigned int ntaps;
	unsigned int ataps;
	int32_t *itaps;
	int32_t *qtaps;
	
	unsigned int owin;
	unsigned int lwin;
	int32_t *win;
	int d;
	
};

/* Audio filter and soft limiter */

struct limiter_t {
	
	/* Input fir filters */
	struct fir_int32_t vfir;
	struct fir_int32_t ffir;
	
	/* Limiter shape */
	int width;
	int16_t *shape;
	
	/* Limiter state */
	int16_t level;
	int32_t *fix;
	int32_t *var;
	int16_t *att;
	int p;
	int h;
	
};

extern void limiter_free(struct limiter_t *s);
extern int limiter_init(struct limiter_t *s, int16_t level, int width, const double *vtaps, const double *ftaps, int ntaps);
extern void limiter_process(struct limiter_t *s, int16_t *out, const int16_t *vin, const int16_t *fin, int samples, int step);

#endif

