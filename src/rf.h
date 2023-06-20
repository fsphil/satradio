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

#ifndef _RF_H
#define _RF_H

/* File output types */
#define RF_UINT8  0
#define RF_INT8   1
#define RF_UINT16 2
#define RF_INT16  3
#define RF_INT32  4
#define RF_FLOAT  5 /* 32-bit float */

/* Audio pre-emphasis modes */
#define RF_PREEMPHASIS_NONE  0
#define RF_PREEMPHASIS_50US  1
#define RF_PREEMPHASIS_75US  2
#define RF_PREEMPHASIS_J17   3

/* Callback prototypes */
typedef int (*rf_write_t)(void *private, int16_t *iq_data, int samples);
typedef int (*rf_close_t)(void *private);

struct rf_t {
	
	void *private;
	rf_write_t write;
	rf_close_t close;
	
	double scale;
	int live;
	
};

extern double rf_scale(struct rf_t *s);
extern int rf_live(struct rf_t *s);
extern int rf_write(struct rf_t *s, int16_t *iq_data, int samples);
extern int rf_close(struct rf_t *s);

/* QPSK modulator (complex output) */

struct rf_qpsk_t {
	
	unsigned int interpolation;
	unsigned int decimation;
	
	unsigned int ntaps;
	unsigned int ataps;
	int16_t *taps;
	
	/* Output window */
	unsigned int owin;
	unsigned int lwin;
	int16_t *iwin;
	int16_t *qwin;
	int d;
	
	/* Differential state */
	int sym;
	
};

extern void rf_qpsk_free(struct rf_qpsk_t *s);
extern int rf_qpsk_init(struct rf_qpsk_t *s, unsigned int interpolation, unsigned int decimation, double level);
extern int rf_qpsk_process(struct rf_qpsk_t *s, int16_t *out, const uint8_t *src, int syms);

/* FM modulator (complex / real output) */
struct rf_fm_t {
	
	int complex_out;
	
	int16_t level;
	int32_t counter;
	int32_t phase[2];
	int32_t *lut;
	
};

extern void rf_fm_free(struct rf_fm_t *s);
extern int rf_fm_init(struct rf_fm_t *s, unsigned int sample_rate, double frequency, double deviation, double level, int complex_out);
extern int rf_fm_process(struct rf_fm_t *s, int16_t *out, const int16_t *in, unsigned int samples);

/* Mixer */

struct rf_mixer_t {
	
	int complex_out;
	
	int16_t level;
	int32_t counter;
	int32_t phase[2];
	int32_t delta[2];
	
};

extern void rf_mixer_free(struct rf_mixer_t *s);
extern int rf_mixer_init(struct rf_mixer_t *s, unsigned int sample_rate, double frequency, double level, int complex_out);
extern int rf_mixer_process(struct rf_mixer_t *s, int16_t *out, const int16_t *in, unsigned int samples);

/* Utils */

extern int64_t rf_gcd(int64_t a, int64_t b);

#include "rf_file.h"

#ifdef HAVE_LIBHACKRF
#include "rf_hackrf.h"
#endif

#ifdef HAVE_SOAPYSDR
#include "rf_soapysdr.h"
#endif

#endif

