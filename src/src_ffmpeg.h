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

#ifndef _SRC_FFMPEG_H
#define _SRC_FFMPEG_H

extern int src_ffmpeg_open(struct src_t *s, const char *input_url, unsigned int sample_rate);

extern void src_ffmpeg_init(void);
extern void src_ffmpeg_deinit(void);

#endif

