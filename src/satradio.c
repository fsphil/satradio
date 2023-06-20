
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include "conf.h"
#include "src.h"
#include "adr.h"
#include "rf.h"
#include "filter.h"

enum satradio_channel_mode_t {
	MODE_FM_MONO,
	MODE_FM_DUAL,
	MODE_ADR,
};

struct satradio_channel_t {
	
	int index;
	
	int active;
	enum satradio_channel_mode_t mode;
	
	/* Audio source */
	struct src_t src;
	unsigned int sample_rate;
	int stereo;
	int repeat;
	
	/* FM filters and modulator */
	struct limiter_t limiter[2];
	struct rf_fm_t fm[2];
	int interp;
	
	/* ADR encoder and modulator */
	struct adr_t adr;
	struct rf_qpsk_t qpsk;
	struct rf_mixer_t mixer;
	
	/* Subcarrier pointers for modulator thread */
	int16_t *subcarrier, *psubcarrier;
	int16_t *subcarrier2, *psubcarrier2;
	int subcarrier_len;
};

#define MAX_CHANNELS 16

struct satradio_t {
	
	conf_t conf;
	int verbose;
	unsigned int sample_rate;
	
	/* Channels */
	struct satradio_channel_t channels[MAX_CHANNELS];
	
	/* Modulator thread */
	struct rf_fm_t fm;
	
	/* Output */
	struct rf_t rf;
};

volatile int _abort = 0;

static void _sigint_callback_handler(int signum)
{
	fprintf(stderr, "Caught signal %d\n", signum);
	
	if(_abort > 0)
	{
		exit(-1);
	}
	
	_abort = 1;
}

static int _channel_src_open(struct satradio_t *s, int channel)
{
	struct satradio_channel_t *ch;
	const char *v;
	int r;
	
	ch = &s->channels[channel];
	
	/* Open the audio source */
	v = conf_str(s->conf, "channel", channel, "type", "rawaudio");
	if(strcmp(v, "rawaudio") == 0)
	{
		v = conf_str(s->conf, "channel", channel, "input", NULL);
		if(!v)
		{
			fprintf(stderr, "Error: Missing input in channel %d.\n", channel + 1);
			return(-1);
		}
		
		r = src_rawaudio_open(
			&ch->src,
			v,
			conf_bool(s->conf, "channel", channel, "exec", 0),
			conf_bool(s->conf, "channel", channel, "stereo", 1)
		);
		
		if(r != 0)
		{
			fprintf(stderr, "Error: Failed to open '%s' for channel %d.\n", v, channel + 1);
			return(-1);
		}
	}
	else if(strcasecmp(v, "tone") == 0)
	{
		r = src_tone_open(
			&ch->src,
			ch->sample_rate,
			conf_double(s->conf, "channel", channel, "tone_hz", 0),
			conf_double(s->conf, "channel", channel, "tone_level", 0)
		);
		
		if(r != 0)
		{
			fprintf(stderr, "Error: Failed to open tone source for channel %d.\n", channel + 1);
			return(-1);
		}
	}
#ifdef HAVE_FFMPEG
	else if(strcasecmp(v, "ffmpeg") == 0)
	{
		v = conf_str(s->conf, "channel", channel, "input", NULL);
		if(!v)
		{
			fprintf(stderr, "Error: Missing input filename/URL for channel %d.\n", channel + 1);
			return(-1);
		}
		
		r = src_ffmpeg_open(&ch->src, v, ch->sample_rate);
		if(r != 0)
		{
			fprintf(stderr, "Error: Failed to open '%s' for channel %d.\n", v, channel + 1);
			return(-1);
		}
	}
#endif
	else
	{
		fprintf(stderr, "Error: Unrecognised input type '%s' for channel %d.\n", v, channel + 1);
		return(-1);
	}
	
	return(0);
}

static int _channel_src_read_mono(struct satradio_t *s, struct satradio_channel_t *ch, int16_t *dst, int step, int samples)
{
	int r;
	
	if(ch->repeat && src_eof(&ch->src))
	{
		src_close(&ch->src);
		
		r = _channel_src_open(s, ch->index);
		if(r != 0)
		{
			return(0);
		}
	}
	
	return(src_read_mono(&ch->src, dst, step, samples));
}

static int _channel_src_read_stereo(struct satradio_t *s, struct satradio_channel_t *ch, int16_t *dst_l, int step_l, int16_t *dst_r, int step_r, int samples)
{
	int r;
	
	if(ch->repeat && src_eof(&ch->src))
	{
		src_close(&ch->src);
		
		r = _channel_src_open(s, ch->index);
		if(r != 0)
		{
			return(0);
		}
	}
	
	return(src_read_stereo(&ch->src, dst_l, step_l, dst_r, step_r, samples));
}

static int _fm_mono_subcarrier(struct satradio_t *s, struct satradio_channel_t *c, int16_t *out, int bl)
{
	int16_t audio[ADR_SAMPLES_PER_FRAME];
	int r;
	
	while(bl > 0)
	{
		if(c->subcarrier_len == 0)
		{
			int l = ADR_SAMPLES_PER_FRAME;
			int16_t *paudio;
			
			paudio = audio;
			
			while(l > 0)
			{
				if(!c->repeat && src_eof(&c->src))
				{
					return(-1);
				}
				
				r = _channel_src_read_mono(s, c, paudio, 1, l);
				if(r == 0)
				{
					break;
				}
				
				paudio += r;
				l -= r;
			}
			
			limiter_process(&c->limiter[0], audio, audio, audio, ADR_SAMPLES_PER_FRAME, 1);
			
			/* Warning: Crude interpolation */
			/* TODO: Do something better here */
			c->subcarrier_len = 0;
			c->psubcarrier = c->subcarrier;
			
			for(r = 0; r < ADR_SAMPLES_PER_FRAME; r++)
			{
				while(c->interp < s->sample_rate)
				{
					*(c->psubcarrier++) = audio[r];
					c->subcarrier_len++;
					c->interp += c->sample_rate;
				}
				
				c->interp -= s->sample_rate;
			}
			
			rf_fm_process(&c->fm[0], c->subcarrier, c->subcarrier, c->subcarrier_len);
			c->psubcarrier = c->subcarrier;
		}
		
		while(bl > 0 && c->subcarrier_len > 0)
		{
			*(out++) += *(c->psubcarrier++);
			c->subcarrier_len--;
			bl--;
		}
	}
	
	return(0);
}

static int _fm_dual_subcarrier(struct satradio_t *s, struct satradio_channel_t *c, int16_t *out, int bl)
{
	int16_t audio[ADR_SAMPLES_PER_FRAME * 2];
	int r;
	
	while(bl > 0)
	{
		if(c->subcarrier_len == 0)
		{
			int l = ADR_SAMPLES_PER_FRAME;
			int16_t *paudio;
			
			paudio = audio;
			
			while(l > 0)
			{
				if(!c->repeat && src_eof(&c->src))
				{
					return(-1);
				}
				
				r = _channel_src_read_stereo(s, c, paudio, 2, paudio + 1, 2, l);
				if(r < 0)
				{
					return(-1);
				}
				
				paudio += r * 2;
				l -= r;
			}
			
			limiter_process(&c->limiter[0], audio, audio, audio, ADR_SAMPLES_PER_FRAME, 2);
			limiter_process(&c->limiter[1], audio + 1, audio + 1, audio + 1, ADR_SAMPLES_PER_FRAME, 2);
			
			/* Warning: Crude interpolation */
			/* TODO: Do something better here */
			c->subcarrier_len = 0;
			c->psubcarrier = c->subcarrier;
			c->psubcarrier2 = c->subcarrier2;
			
			for(r = 0; r < ADR_SAMPLES_PER_FRAME; r++)
			{
				while(c->interp < s->sample_rate)
				{
					*(c->psubcarrier++) = audio[r * 2 + 0];
					*(c->psubcarrier2++) = audio[r * 2 + 1];
					c->subcarrier_len++;
					c->interp += c->sample_rate;
				}
				
				c->interp -= s->sample_rate;
			}
			
			rf_fm_process(&c->fm[0], c->subcarrier, c->subcarrier, c->subcarrier_len);
			rf_fm_process(&c->fm[1], c->subcarrier2, c->subcarrier2, c->subcarrier_len);
			c->psubcarrier = c->subcarrier;
			c->psubcarrier2 = c->subcarrier2;
		}
		
		while(bl > 0 && c->subcarrier_len > 0)
		{
			*(out++) += *(c->psubcarrier++) + *(c->psubcarrier2++);
			c->subcarrier_len--;
			bl--;
		}
	}
	
	return(0);
}


static int _adr_subcarrier(struct satradio_t *s, struct satradio_channel_t *c, int16_t *out, int bl)
{
	int16_t audio[ADR_SAMPLES_PER_FRAME * 2];
	uint8_t frame[ADR_FRAME_BYTES];
	int r;
	
	while(bl > 0)
	{
		if(c->subcarrier_len == 0)
		{
			int l = ADR_SAMPLES_PER_FRAME;
			int16_t *paudio;
			
			paudio = audio;
			
			while(l > 0)
			{
				if(!c->repeat && src_eof(&c->src))
				{
					return(-1);
				}
				
				if(c->stereo)
				{
					r = _channel_src_read_stereo(s, c, paudio, 2, paudio + 1, 2, l);
					paudio += r * 2;
				}
				else
				{
					r = _channel_src_read_mono(s, c, paudio, 1, l);
					paudio += r;
				}
				
				if(r == 0)
				{
					break;
				}
				
				l -= r;
			}
			
			if(c->stereo)
			{
				adr_feed(&c->adr, audio, 2, audio + 1, 2, ADR_SAMPLES_PER_FRAME);
			}
			else
			{
				adr_feed(&c->adr, audio, 1, NULL, 0, ADR_SAMPLES_PER_FRAME);
			}
			
			while(adr_next_frame(&c->adr, frame) == 0)
			{
				r = rf_qpsk_process(&c->qpsk, c->subcarrier, frame, ADR_FRAME_SYMS);
				rf_mixer_process(&c->mixer, c->subcarrier, c->subcarrier, r);
				
				c->subcarrier_len = r;
				c->psubcarrier = c->subcarrier;
			}
		}
		
		while(bl > 0 && c->subcarrier_len > 0)
		{
			*(out++) += *(c->psubcarrier++);
			c->subcarrier_len--;
			bl--;
		}
	}
	
	return(0);
}

static void print_usage(void)
{
	printf(
		"\n"
		"Usage: satradio [options]\n"
		"\n"
		"  -c, --config <file>      Load configuration from file.\n"
		"  -v, --verbose            Enable verbose output.\n"
		"\n"
	);
}

static int _modulate_channel(struct satradio_t *s, struct satradio_channel_t *c, int16_t *out, int bl)
{
	int r;
	
	if(!c->active)
	{
		return(-1);
	}
	
	if(c->mode == MODE_FM_MONO)
	{
		r = _fm_mono_subcarrier(s, c, out, bl);
	}
	else if(c->mode == MODE_FM_DUAL)
	{
		r = _fm_dual_subcarrier(s, c, out, bl);
	}
	else if(c->mode == MODE_ADR)
	{
		r = _adr_subcarrier(s, c, out, bl);
	}
	else
	{
		return(-1);
	}
	
	if(r != 0)
	{
		c->active = 0;
		return(-1);
	}
	
	return(0);
}

static int _main_loop(struct satradio_t *s)
{
	int16_t *sum;
	int16_t *out;
	int i, a, bl;
	
	/* Allocate enough memory for 100ms sum and output buffers */
	bl = s->sample_rate / 10;
	sum = calloc(sizeof(int16_t), bl);
	out = calloc(sizeof(int16_t) * 2, bl);
	
	if(!sum || !out)
	{
		free(sum);
		free(out);
		fprintf(stderr, "Out of memory.\n");
		return(-1);
	}
	
	while(!_abort)
	{
		/* Poll each active channel and read 100ms of signal */
		memset(sum, 0, bl * sizeof(int16_t));
		
		for(a = i = 0; i < MAX_CHANNELS; i++)
		{
			if(_modulate_channel(s, &s->channels[i], sum, bl) == 0) a++;
		}
		
		/* End if there are no active stations */
		if(a == 0) break;
		
		/* FM modulate the source */
		rf_fm_process(&s->fm, out, sum, bl);
		
		/* Output to the radio */
		rf_write(&s->rf, out, bl);
	}
	
	free(sum);
	free(out);
	
	return(0);
}

int main(int argc, char *argv[])
{
	struct satradio_t s;
	const char *conffile = NULL;
	int c, option_index;
	const struct option long_options[] = {
		{ "version", no_argument,       0, 'v' },
		{ "config",  required_argument, 0, 'c' },
		{ "verbose", no_argument,       0, 'V' },
		{ 0, 0, 0, 0 }
	};
	int i, r;
	const char *v;
	
#ifdef HAVE_FFMPEG
	src_ffmpeg_init();
#endif
	
	memset(&s, 0, sizeof(struct satradio_t));
	
	opterr = 0;
	while((c = getopt_long(argc, argv, "vc:V", long_options, &option_index)) != -1)
	{
		switch(c)
		{
		case 'v': /* -v, --version */
			fprintf(stderr, "satradio v0.2\n");
			return(0);
		
		case 'c': /* -c, --config <filename> */
			conffile = optarg;
			break;
		
		case 'V': /* -V, --verbose */
			s.verbose = 1;
			break;
		
		case '?':
			print_usage();
			return(0);
		}
	}
	
	if(!conffile)
	{
		fprintf(stderr, "No configuration file specified\n");
		return(-1);
	}
	
	s.conf = conf_loadfile(conffile);
	if(!s.conf)
	{
		/* Configuration error */
		fprintf(stderr, "Failed to load configuration file '%s'.\n", conffile);
		return(-1);
	}
	
	s.verbose = conf_bool(s.conf, NULL, -1, "verbose", s.verbose);
	s.sample_rate = conf_double(s.conf, "output", -1, "sample_rate", 0),
	
	/* Catch all the signals */
	signal(SIGINT, &_sigint_callback_handler);
	signal(SIGILL, &_sigint_callback_handler);
	signal(SIGFPE, &_sigint_callback_handler);
	signal(SIGSEGV, &_sigint_callback_handler);
	signal(SIGTERM, &_sigint_callback_handler);
	signal(SIGABRT, &_sigint_callback_handler);
	
	/* Load configuration for each channel */
	for(i = 0; conf_section_exists(s.conf, "channel", i); i++)
	{
		struct satradio_channel_t *ch;
		
		/* Enforce the channel limit */
		if(i >= MAX_CHANNELS)
		{
			fprintf(stderr, "Warning: This version of satradio supports a maximum of %d channels.", MAX_CHANNELS);
			break;
		}
		
		ch = &s.channels[i];
		ch->index = i;
		
		/* Configure the channel */
		v = conf_str(s.conf, "channel", i, "mode", NULL);
		if(v == NULL)
		{
			fprintf(stderr, "Error: No mode specified for channel %d.\n", i + 1);
			return(-1);
		}
		else if(strcmp(v, "fm") == 0)
		{
			const double *taps = NULL;
			
			ch->mode = MODE_FM_MONO;
			
			v = conf_str(s.conf, "channel", i, "preemphasis", "none");
			if(strcmp(v, "none") == 0)
			{
				taps = preemph_flat_taps;
			}
			else if(strcmp(v, "50us") == 0)
			{
				taps = preemph_50us_taps;
			}
			else if(strcmp(v, "75us") == 0)
			{
				taps = preemph_75us_taps;
			}
			else if(strcmp(v, "j17") == 0)
			{
				taps = preemph_j17_taps;
			}
			else
			{
				fprintf(stderr, "Error: Unrecognised pre-emphasis mode '%s' for channel %d.\n", v, i + 1);
				return(-1);
			}
			
			r = rf_fm_init(&ch->fm[0],
				s.sample_rate,
				conf_double(s.conf, "channel", i, "frequency", 0),
				conf_double(s.conf, "channel", i, "deviation", 50e3),
				conf_double(s.conf, "channel", i, "level", 1),
				0 /* Real output */
			);
			
			if(r != 0)
			{
				fprintf(stderr, "Error: Failed to initalise FM modulator for channel %d.\n", i + 1);
				return(-1);
			}
			
			r = limiter_init(&ch->limiter[0], INT16_MAX, 21, taps, preemph_flat_taps, PREEMPH_TAPS);
			if(r != 0)
			{
				fprintf(stderr, "Error: Unable to initalise pre-emphasis filter for channel %d.\n", i + 1);
				return(-1);
			}
			
			ch->sample_rate = 32000;
			ch->stereo = 0;
			
			/* Allocate memory for modulator output buffer */
			r = ch->sample_rate / TWOLAME_SAMPLES_PER_FRAME;
			r = (s.sample_rate + r - 1) / r;
			ch->subcarrier = malloc(sizeof(int16_t) * 2 * r);
			if(!ch->subcarrier)
			{
				fprintf(stderr, "Out of memory.\n");
				return(-1);
			}
		}
		else if(strcmp(v, "dual-fm") == 0)
		{
			const double *taps = NULL;
			
			ch->mode = MODE_FM_DUAL;
			
			v = conf_str(s.conf, "channel", i, "preemphasis", "none");
			if(strcmp(v, "none") == 0)
			{
				taps = preemph_flat_taps;
			}
			else if(strcmp(v, "50us") == 0)
			{
				taps = preemph_50us_taps;
			}
			else if(strcmp(v, "75us") == 0)
			{
				taps = preemph_75us_taps;
			}
			else if(strcmp(v, "j17") == 0)
			{
				taps = preemph_j17_taps;
			}
			else
			{
				fprintf(stderr, "Error: Unrecognised pre-emphasis mode '%s' for channel %d.\n", v, i + 1);
				return(-1);
			}
			
			for(c = 0; c < 2; c++)
			{
				r = rf_fm_init(&ch->fm[c],
					s.sample_rate,
					conf_double(s.conf, "channel", i, c == 0 ? "frequency1" : "frequency2", 0),
					conf_double(s.conf, "channel", i, "deviation", 50e3),
					conf_double(s.conf, "channel", i, "level", 1),
					0 /* Real output */
				);
				
				if(r != 0)
				{
					fprintf(stderr, "Error: Failed to initalise FM modulator for channel %d.\n", i + 1);
					return(-1);
				}
				
				r = limiter_init(&ch->limiter[c], INT16_MAX, 21, taps, preemph_flat_taps, PREEMPH_TAPS);
				if(r != 0)
				{
					fprintf(stderr, "Error: Unable to initalise pre-emphasis filter for channel %d.\n", i + 1);
					return(-1);
				}
			}
			
			ch->sample_rate = 32000;
			ch->stereo = 1;
			
			/* Allocate memory for modulator output buffer */
			r = ch->sample_rate / TWOLAME_SAMPLES_PER_FRAME;
			r = (s.sample_rate + r - 1) / r;
			ch->subcarrier = malloc(sizeof(int16_t) * 2 * r);
			ch->subcarrier2 = malloc(sizeof(int16_t) * 2 * r);
			if(!ch->subcarrier || !ch->subcarrier2)
			{
				fprintf(stderr, "Out of memory.\n");
				return(-1);
			}
		}
		else if(strcmp(v, "adr") == 0)
		{
			TWOLAME_MPEG_mode mode;
			
			ch->mode = MODE_ADR;
			
			v = conf_str(s.conf, "channel", i, "adr_mode", "joint");
			if(strcmp("mono", v) == 0) mode = TWOLAME_MONO;
			else if(strcmp("dual", v) == 0) mode = TWOLAME_DUAL_CHANNEL;
			else if(strcmp("joint", v) == 0) mode = TWOLAME_JOINT_STEREO;
			else if(strcmp("stereo", v) == 0) mode = TWOLAME_STEREO;
			else
			{
				fprintf(stderr, "Error: Unrecognised ADR mode '%s' for channel %d.\n", v, i + 1);
				return(-1);
			}
			
			/* Initalise ADR encoder */
			r = adr_init(&ch->adr, mode, conf_bool(s.conf, "channel", i, "scfcrc", 1));
			if(r != 0)
			{
				fprintf(stderr, "Error: ADR encoder failed to initalise for channel %d.\n", i + 1);
				return(-1);
			}
			
			/* Set the channel name */
			adr_set_station_id(&ch->adr, conf_str(s.conf, "channel", i, "name", ""));
			
			/* Initalise QPSK modulator and mixer */
			r = rf_gcd(s.sample_rate, ADR_SYMBOL_RATE);
			
			rf_qpsk_init(&ch->qpsk, s.sample_rate / r, ADR_SYMBOL_RATE / r, 1);
			
			rf_mixer_init(&ch->mixer, s.sample_rate,
				conf_double(s.conf, "channel", i, "frequency", 0),
				conf_double(s.conf, "channel", i, "level", 1),
				0 /* Real output */
			);
			
			ch->sample_rate = ADR_SAMPLE_RATE;
			ch->stereo = (mode == TWOLAME_MONO ? 0 : 1);
			
			/* Allocate memory for modulator output buffer */
			r = ch->sample_rate / TWOLAME_SAMPLES_PER_FRAME;
			r = (s.sample_rate + r - 1) / r;
			ch->subcarrier = malloc(sizeof(int16_t) * 2 * r);
			if(!ch->subcarrier)
			{
				fprintf(stderr, "Out of memory.\n");
				return(-1);
			}
		}
		else
		{
			fprintf(stderr, "Error: Unsupported channel mode '%s' for channel %d.", v, i + 1);
			return(-1);
		}
		
		/* Open the audio source */
		ch->repeat = conf_bool(s.conf, "channel", i, "repeat", 0);
		
		r = _channel_src_open(&s, i);
		if(r != 0)
		{
			return(-1);
		}
		
		ch->active = 1;
	}
	
	/* Start the radio / output */
	v = conf_str(s.conf, "output", -1, "type", NULL);
	
	if(v == NULL)
	{
		fprintf(stderr, "No output type specified.\n");
		return(-1);
	}
	
	if(strcmp(v, "file") == 0)
	{
		i = -1;
		v = conf_str(s.conf, "output", -1, "data_type", "int16");
		
		if(strcmp(v, "uint8") == 0)       i = RF_UINT8;
		else if(strcmp(v, "int8") == 0)   i = RF_INT8;
		else if(strcmp(v, "uint16") == 0) i = RF_UINT16;
		else if(strcmp(v, "int16") == 0)  i = RF_INT16;
		else if(strcmp(v, "int32") == 0)  i = RF_INT32;
		else if(strcmp(v, "float") == 0)  i = RF_FLOAT;
		else
		{
			fprintf(stderr, "Error: Invalid data type '%s'.\n", v);
			return(-1);
		}
		
		r = rf_file_open(&s.rf,
			conf_str(s.conf, "output", -1, "output", NULL),
			i,
			conf_bool(s.conf, "output", -1, "live", 0)
		);
		
		if(r != 0)
		{
			return(-1);
		}
	}
#ifdef HAVE_LIBHACKRF
	else if(strcmp(v, "hackrf") == 0)
	{
		r = rf_hackrf_open(&s.rf,
			conf_str(s.conf, "output", -1, "output", NULL),
			s.sample_rate,
			conf_double(s.conf, "output", -1, "frequency", 0),
			conf_double(s.conf, "output", -1, "gain", 0),
			conf_bool(s.conf, "output", -1, "amp", 0)
		);
		
		if(r != 0)
		{
			return(-1);
		}
	}
#endif
#ifdef HAVE_SOAPYSDR
	else if(strcmp(v, "soapysdr") == 0)
	{
		r = rf_soapysdr_open(&s.rf,
			conf_str(s.conf, "output", -1, "output", NULL),
			s.sample_rate,
			conf_double(s.conf, "output", -1, "frequency", 0),
			conf_double(s.conf, "output", -1, "gain", 0),
			conf_str(s.conf, "output", -1, "antenna", NULL)
		);
		
		if(r != 0)
		{
			return(-1);
		}
	}
#endif
	else
	{
		fprintf(stderr, "Unrecognised output type.\n");
		return(-1);
	}
	
	/* Configure output FM modulator (100ms blocks) */
	rf_fm_init(&s.fm,
		s.sample_rate,
		0, /* No frequency offset */
		conf_double(s.conf, "output", -1, "deviation", 16e6),
		conf_double(s.conf, "output", -1, "level", 1.0) * rf_scale(&s.rf),
		1 /* Complex output */
	);
	
	_main_loop(&s);
	
	/* Close the output */
	rf_close(&s.rf);
	
#ifdef HAVE_FFMPEG
	src_ffmpeg_deinit();
#endif
	
	free(s.conf);
	
	return(0);
}

