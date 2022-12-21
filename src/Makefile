
# -fsanitize=address
CC      := $(CROSS_HOST)gcc
PKGCONF := $(CROSS_HOST)pkg-config
CFLAGS  := -g -Wall -O3 -pthread -pedantic $(EXTRA_CFLAGS)
LDFLAGS := -g -lm -pthread $(EXTRA_LDFLAGS)
OBJS    := satradio.o conf.o rf.o rf_file.o src.o src_tone.o src_rawaudio.o filter.o adr.o
PKGS    := twolame

FFMPEG := $(shell $(PKGCONF) --exists libavcodec && echo ffmpeg)
ifeq ($(FFMPEG),ffmpeg)
	OBJS += src_ffmpeg.o
	PKGS += libavcodec libavformat libavdevice libswresample libavutil
	CFLAGS += -DHAVE_FFMPEG
endif

HACKRF := $(shell $(PKGCONF) --exists libhackrf && echo hackrf)
ifeq ($(HACKRF),hackrf)
	OBJS += rf_hackrf.o
	PKGS += libhackrf
	CFLAGS += -DHAVE_LIBHACKRF
endif

SOAPYSDR := $(shell $(PKGCONF) --exists SoapySDR && echo SoapySDR)
ifeq ($(SOAPYSDR),SoapySDR)
	OBJS += rf_soapysdr.o
	PKGS += SoapySDR
	CFLAGS += -DHAVE_SOAPYSDR
endif

PKGS    += $(EXTRA_PKGS)
CFLAGS  += $(shell $(PKGCONF) --cflags $(PKGS))
LDFLAGS += $(shell $(PKGCONF) --libs $(PKGS))

all: satradio

satradio: $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@
	@$(CC) $(CFLAGS) -MM $< -o $(@:.o=.d)

clean:
	rm -f *.o *.d satradio satradio.exe

-include $(OBJS:.o=.d)

