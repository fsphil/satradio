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

#ifndef _ADR_H
#define _ADR_H

#include <stdint.h>
#include <twolame.h>

/* ADR bit and symbol rates */
#define ADR_BIT_RATE    256000
#define ADR_SYMBOL_RATE (ADR_BIT_RATE / 2)

#define ADR_FRAME_BYTES 768
#define ADR_FRAME_BITS  (ADR_FRAME_BYTES * 8)
#define ADR_FRAME_SYMS  (ADR_FRAME_BITS / 2)

/* Samples per frame */
#define ADR_SAMPLES_PER_FRAME TWOLAME_SAMPLES_PER_FRAME

/* ADR uses 48kHz sample rate at 192kbit/s, producing a 576 byte frame */
#define ADR_MP2_FRAME_LEN 576

/* ADR has a fixed 48000 Hz audio sample rate */
#define ADR_SAMPLE_RATE 48000

struct adr_t {
	
	/* config */
	TWOLAME_MPEG_mode mode;
	uint8_t station_id[32];
	int scfcrc;
	
	/* audio input */
	const int16_t *left_audio;
	const int16_t *right_audio;
	int left_step;
	int right_step;
	int audio_samples;
	
	/* twolame encoder */
	twolame_options *encopts;
	int16_t mp2audio[TWOLAME_SAMPLES_PER_FRAME * 2];
	int mp2audio_samples;
	uint8_t mp2buffer[2][ADR_MP2_FRAME_LEN + 1];
	int frame;
	
	/* ancillary data */
	char *cptr, cmsg[40]; /* The currently transmitting control message */
	int cindex; /* Index of the current message */
	char dc4_mode;
	
	/* frame scrambler */
	uint32_t ssr;	/* shift register */
	uint8_t sc;	/* counter */
	
	/* fec */
	uint8_t b;
	uint8_t sr;	/* shift register */
};

extern int adr_init(struct adr_t *adr, TWOLAME_MPEG_mode mode, int scfcrc);
extern void adr_set_station_id(struct adr_t *adr, const char *station_id);
extern int adr_feed(struct adr_t *adr, const int16_t *left, int left_step, const int16_t *right, int right_step, int samples);
extern int adr_next_frame(struct adr_t *adr, uint8_t *frame);
extern int adr_last_frame(struct adr_t *adr, uint8_t *frame);
extern int adr_free(struct adr_t *s);

#endif

