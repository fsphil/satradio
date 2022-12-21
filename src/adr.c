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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <twolame.h>
#include "adr.h"

/* EBU Latin character set */
static const char *_charset[256] = {
	"","","","","","","","","","","","","","","","",
	"","","","","","","","","","","","","","","","",
	" ","!","\"","#","¤","%","&","'","(",")","*","+",",","-",".","/",
	"0","1","2","3","4","5","6","7","8","9",":",";","<","=",">","?",
	"@","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O",
	"P","Q","R","S","T","U","V","W","X","Y","Z","[","\\","]","―","_",
	"‖","a","b","c","d","e","f","g","h","i","j","k","l","m","n","o",
	"p","q","r","s","t","u","v","w","x","y","z","{","|","}","¯","",
	"á","à","é","è","í","ì","ó","ò","ú","ù","Ñ","Ç","Ş","β","¡","Ĳ",
	"â","ä","ê","ë","î","ï","ô","ö","û","ü","ñ","ç","ş","ǧ","ı","ĳ",
	"ª","α","©","‰","Ǧ","ě","ň","ő","π","€","£","$","←","↑","→","↓",
	"º","¹","²","³","±","İ","ń","ű","µ","¿","÷","°","¼","½","¾","§",
	"Á","À","É","È","Í","Ì","Ó","Ò","Ú","Ù","Ř","Č","Š","Ž","Ð","Ŀ",
	"Â","Ä","Ê","Ë","Î","Ï","Ô","Ö","Û","Ü","ř","č","š","ž","đ","ŀ",
	"Ã","Å","Æ","Œ","ŷ","Ý","Õ","Ø","Þ","Ŋ","Ŕ","Ć","Ś","Ź","Ŧ","ð",
	"ã","å","æ","œ","ŵ","ý","õ","ø","þ","ŋ","ŕ","ć","ś","ź","ŧ","",
};

static uint32_t _utf8next(const char *str, const char **next)
{
	const uint8_t *c;
	uint32_t u, m;
	uint8_t b;
	
	/* Read and return a utf-8 character from str.
	 * If next is not NULL, it is pointed to the next
	 * character following this.
	 * 
	 * If an invalid code is detected, the function
	 * returns U+FFFD, � REPLACEMENT CHARACTER.
	*/
	
	c = (const uint8_t *) str;
	if(next) *next = str + 1;
	
	/* Shortcut for single byte codes */
	if(*c < 0x80) return(*c);
	
	/* Find the code length, initial bits and the first valid code */
	if((*c & 0xE0) == 0xC0) { u = *c & 0x1F; b = 1; m = 0x00080; }
	else if((*c & 0xF0) == 0xE0) { u = *c & 0x0F; b = 2; m = 0x00800; }
	else if((*c & 0xF8) == 0xF0) { u = *c & 0x07; b = 3; m = 0x10000; }
	else return(0xFFFD);
	
	while(b--)
	{
		/* All bytes after the first must begin 0x10xxxxxx */
		if((*(++c) & 0xC0) != 0x80) return(0xFFFD);
		
		/* Add the 6 new bits to the code */
		u = (u << 6) | (*c & 0x3F);
		
		/* Advance next pointer */
		if(next) (*next)++;
	}
	
	/* Reject overlong encoded characters */
	if(u < m) return(0xFFFD);
	
	return(u);
}

static void _encode_ebu_string(uint8_t *dst, const char *src, int len)
{
	uint32_t c;
	int i, j;
	
	for(i = 0; i < len && *src; i++)
	{
		c = _utf8next(src, &src);
		
		/* Lookup EBU character set for a match */
		for(j = 0; j < 256; j++)
		{
			if(c == _utf8next(_charset[j], NULL)) break;
		}
		
		/* Write character, or ' ' if not recognised */
		dst[i] = (j == 256 ? ' ' : j);
	}
	
	for(; i < len; i++)
	{
		dst[i] = '\0';
	}
}

void _decode_ebu_string(char *dst, const uint8_t *src, int len)
{
	int i;
	
	for(*dst = '\0', i = 0; *src && i < len; i++, src++)
	{
		strcat(dst, _charset[*src][0] ? _charset[*src] : "?");
	}
}

/* (7,4) Block Code lookup table */
static const uint8_t _74code[16] = {
	0x00,0x07,0x19,0x1E,0x2A,0x2D,0x33,0x34,0x4B,0x4C,0x52,0x55,0x61,0x66,0x78,0x7F,
};

static uint8_t _scramble(struct adr_t *adr, uint8_t in)
{
	uint8_t out;
	
	/* IDR/IBS CCITT V.35 scrambler
	 * 
	 * adv = 1 when 5-bit counter == 0x1F
	 * out = 1 xor in xor 20th xor 3rd xor adv
	 * 
	 * 5-bit counter is reset if 9th xor 1st == 1, or it overflowed
	 * 
	 * Output is feed back into shift register. To change
	 * this to a descrambler, feed in the input bit instead
	*/
	
	out = (1 ^ in ^ adr->ssr ^ (adr->ssr >> 17) ^ (++adr->sc >> 5)) & 1;
	if((adr->sc > 0x1F) | (((adr->ssr >> 19) ^ (adr->ssr >> 11)) & 1)) adr->sc = 0;
	adr->ssr = (adr->ssr >> 1) | (out << 19);
	
	return(out);
}

static void _fec(struct adr_t *adr, uint8_t dst[ADR_FRAME_BYTES], const uint8_t src[ADR_MP2_FRAME_LEN])
{
	int i, pi, pq;
	int b;
	
	memset(dst, 0, ADR_FRAME_BYTES);
	
	pi = 0;
	pq = 1;
	
	/* Apply 1/2 convolutional encoding with 3/4 puncture code */
	for(i = 0; i < ADR_MP2_FRAME_LEN * 8; i++)
	{
		/* Update shift register, reading MSB first */
		adr->b ^= _scramble(adr, (src[i >> 3] >> (7 - (i & 7))) & 1);
		adr->sr = (adr->sr >> 1) | adr->b << 6;
		
		/* Output punctured I data (generator: 1111001, puncture: 101) */
		if(i % 3 != 1)
		{
			b = (adr->sr ^ (adr->sr >> 3) ^ (adr->sr >> 4) ^ (adr->sr >> 5) ^ (adr->sr >> 6)) & 1;
			dst[pi >> 3] |= b << (7 - (pi & 7));
			pi += 2;
		}
		
		/* Output punctured Q data (generator: 1011011, puncture: 110) */
		if(i % 3 != 2)
		{
			b = (adr->sr ^ (adr->sr >> 1) ^ (adr->sr >> 3) ^ (adr->sr >> 4) ^ (adr->sr >> 6)) & 1;
			dst[pq >> 3] |= b << (7 - (pq & 7));
			pq += 2;
		}
	}
}

static void _insert_adr_ancillary(struct adr_t *adr, uint8_t *data)
{
	uint8_t ad[18];
	uint8_t cw[36];
	int i, b;
	
	/* ADR ancillary data is built from 18 data bytes */
	memset(ad, 0, 18);
	
	/* Control data */
	for(i = 15; i < 18; i++)
	{
		if(*adr->cptr == '\0')
		{
			uint8_t check;
			
			/* Generate the next message */
			switch(adr->cindex++)
			{
			case 0: /* DC1 - Free-to-air service */
				snprintf(adr->cmsg, 39, "\x02\x11\x04");
				break;
			
			case 1: /* DC4 - Program information */
				/* E1 = Extended Country Code */
				/* C  = Country Code */
				/* 2  = Coverage Area Code */
				/* 0A = Program Reference Number */
				/* %c = M/S/A/B = Mode */
				/* 2  = Program Category */
				snprintf(adr->cmsg, 39, "\x02\x14" "E1C20A%c2" "\x04", adr->dc4_mode);
				break;
			
			case 2: /* SYN - Station ID information */
				/* Padding is added to keep the station ID to at least 8 characters.
				 * The Astrastar AX 1 does not display anything if it's less than this.
				*/
				snprintf(adr->cmsg, 39, "\x02\x16" "%-8s#" "\x04", adr->station_id);
				adr->cindex = 0;
				break;
			}
			
			/* Calculate the checksum */
			for(adr->cptr = adr->cmsg, check = 0x00; *adr->cptr; adr->cptr++)
			{
				check += *adr->cptr & 0x7F;
			}
			
			/* Append the checksum and end of message byte */
			snprintf(adr->cptr, 4, "%1X%1X\x03", check & 0x0F, check >> 4);
			
			/* Reset the pointer */
			adr->cptr = adr->cmsg;
		}
		
		ad[i] = *adr->cptr++;
	}
	
	/* Control flags (MSBs of the 3 control data bytes) */
	ad[15] |= 0 << 7; /* 1 == Start of key period for pay service smart card decryption */
	ad[16] |= 0 << 7; /* 1 == RDS data and auxiliary data are complemented in this frame */
	ad[17] |= adr->scfcrc << 7; /* 1 == Scale factor CRC is present */
	
	/* Generate the 36 codewords */
	for(i = 0; i < 18; i++)
	{
		cw[i * 2 + 0] = _74code[ad[i] & 0x0F];
		cw[i * 2 + 1] = _74code[ad[i] >> 4];
	}
	
	/* Write the codewords into the ancillary data, interleaved 36x7 */
	data += 0x21C;
	for(i = 0; i < 252; i++)
	{
		/* Writing to byte b, skipping the SCF-CRC */
		b = (i >> 3);
		if(b >= 30) b += 4;
		
		data[b] |= ((cw[i % 36] >> (i / 36)) & 1) << (7 - (i & 7));
	}
}

static uint8_t *_encode_adr_frame(struct adr_t *adr, const int16_t *pcm)
{
	int r;
	uint8_t *fn = adr->mp2buffer[(adr->frame + 1) & 1];
	uint8_t *fl = adr->mp2buffer[(adr->frame + 0) & 1];
	uint8_t *mp2 = NULL;
	
	if(pcm)
	{
		r = twolame_encode_buffer_interleaved(adr->encopts, pcm, TWOLAME_SAMPLES_PER_FRAME, fn, ADR_MP2_FRAME_LEN + 1);
	}
	else
	{
		r = twolame_encode_flush(adr->encopts, fn, ADR_MP2_FRAME_LEN + 1);
	}
	
	if(r > 0)
	{
		_insert_adr_ancillary(adr, fn);
		
		if(adr->scfcrc == 0)
		{
			mp2 = fn;
		}
		else if(adr->frame > 0)
		{
			/* The ScF CRC of each frame is stored in the previous frame */
			twolame_set_DAB_scf_crc(adr->encopts, fl, r);
			mp2 = fl;
		}
		
		adr->frame++;
	}
	
	return(mp2);
}

int adr_init(struct adr_t *adr, TWOLAME_MPEG_mode mode, int scfcrc)
{
	int r;
	
	memset(adr, 0, sizeof(struct adr_t));
	
	adr->mode = mode;
	
	switch(adr->mode)
	{
	case TWOLAME_MONO: adr->dc4_mode = 'M'; break;
	case TWOLAME_DUAL_CHANNEL: adr->dc4_mode = 'A'; break;
	case TWOLAME_JOINT_STEREO: adr->dc4_mode = 'S'; break;
	case TWOLAME_STEREO: adr->dc4_mode = 'S'; break;
	default:
		fprintf(stderr, "adr_init(): Unrecognised mode %d\n", adr->mode);
		return(-1);
	}
	
	adr->cmsg[0] = '\0';
	adr->cptr = adr->cmsg;
	adr->cindex = 0;
	
	adr->scfcrc = scfcrc ? 1 : 0;
	
	/* Configure twolame */
	adr->encopts = twolame_init();
	twolame_set_in_samplerate(adr->encopts, 48000);
	twolame_set_out_samplerate(adr->encopts, 48000);
	twolame_set_bitrate(adr->encopts, 192);
	twolame_set_num_channels(adr->encopts, adr->mode == TWOLAME_MONO ? 1 : 2);
	twolame_set_mode(adr->encopts, adr->mode);
	twolame_set_error_protection(adr->encopts, TRUE);
	twolame_set_num_ancillary_bits(adr->encopts, 36 * 8);
	if(adr->scfcrc)
	{
		twolame_set_DAB(adr->encopts, TRUE);
		twolame_set_DAB_scf_crc_length(adr->encopts);
	}
	r = twolame_init_params(adr->encopts);
	if(r != 0) return(r);
	
	return(0);
}

void adr_set_station_id(struct adr_t *adr, const char *station_id)
{
	_encode_ebu_string(adr->station_id, station_id, 32);
}

int adr_feed(struct adr_t *adr, const int16_t *left, int left_step, const int16_t *right, int right_step, int samples)
{
	adr->left_audio = left;
	adr->right_audio = right;
	adr->left_step = left_step;
	adr->right_step = right_step;
	adr->audio_samples = samples;
	return(0);
}

int adr_next_frame(struct adr_t *adr, uint8_t *frame)
{
	int16_t *pcm;
	const uint8_t *mp2;
	
	pcm = &adr->mp2audio[adr->mp2audio_samples * (adr->mode == TWOLAME_MONO ? 1 : 2)];
	
	while(adr->mp2audio_samples < TWOLAME_SAMPLES_PER_FRAME)
	{
		if(adr->audio_samples == 0)
		{
			return(-1);
		}
		
		*(pcm++) = *adr->left_audio;
		adr->left_audio += adr->left_step;
		
		if(adr->mode != TWOLAME_MONO)
		{
			*(pcm++) = *adr->right_audio;
			adr->right_audio += adr->right_step;
		}
		
		adr->audio_samples--;
		adr->mp2audio_samples++;
	}
	
	mp2 = _encode_adr_frame(adr, adr->mp2audio);
	adr->mp2audio_samples = 0;
	
	if(mp2 == NULL)
	{
		return(-1);
	}
	
	/* Apply scrambler and FEC to the output */
	_fec(adr, frame, mp2);
	
	return(0);
}

int adr_last_frame(struct adr_t *adr, uint8_t *frame)
{
	const uint8_t *mp2;
	
	/* Flush the last frame */
	mp2 = _encode_adr_frame(adr, NULL);
	adr->mp2audio_samples = 0;
	
	if(mp2 == NULL)
	{
		return(-1);
	}
	
	/* Apply scrambler and FEC to the output */
	_fec(adr, frame, mp2);
	
	return(0);
}

int adr_free(struct adr_t *adr)
{
	/* Tidy up */
	twolame_close(&adr->encopts);
	
	return(0);
}

