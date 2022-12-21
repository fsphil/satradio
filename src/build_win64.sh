#!/bin/bash

set -e
set -x

HOST=x86_64-w64-mingw32
PREFIX=$(pwd)/build_win64/install_root
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig

mkdir -p $PREFIX
cd build_win64

# libusb
if [[ ! -f $PREFIX/lib/libusb-1.0.a ]]; then
	
	if [[ ! -d libusb ]]; then
		git clone --depth 1 https://github.com/libusb/libusb.git
	fi

	cd libusb
	./autogen.sh --host=$HOST --prefix=$PREFIX --enable-static --disable-shared
	make -j4 install
	cd ..
fi

# hackrf
if [[ ! -f $PREFIX/lib/libhackrf.a ]]; then

	if [[ ! -d hackrf ]]; then
		git clone --depth 1 https://github.com/greatscottgadgets/hackrf.git
	fi
	
	rm -rf hackrf/host/libhackrf/build
	mkdir -p hackrf/host/libhackrf/build
	cd hackrf/host/libhackrf/build
	cmake .. \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER=$HOST-gcc \
		-DCMAKE_INSTALL_PREFIX=$PREFIX \
		-DCMAKE_INSTALL_LIBPREFIX=$PREFIX/lib \
		-DLIBUSB_INCLUDE_DIR=$PREFIX/include/libusb-1.0 \
		-DLIBUSB_LIBRARIES=$PREFIX/lib/libusb-1.0.a
	make -j4 install
	cd ../../../..
	mv $PREFIX/bin/*.a $PREFIX/lib/
	find $PREFIX -name libhackrf\*.dll\* -delete
fi

# AAC codec
if [[ ! -f $PREFIX/lib/libfdk-aac.a ]]; then
	
	if [[ ! -d fdk-aac ]]; then
		git clone https://github.com/mstorsjo/fdk-aac.git
	fi
	
	cd fdk-aac
	./autogen.sh
	./configure --host=$HOST --prefix=$PREFIX --enable-static --disable-shared
	make -j4 install
	cd ..
fi

# opus codec
if [[ ! -f $PREFIX/lib/libopus.a ]]; then
	
	if [[ ! -f opus-1.3.1.tar.gz ]]; then
		wget https://archive.mozilla.org/pub/opus/opus-1.3.1.tar.gz
		tar -xvzf opus-1.3.1.tar.gz
	fi
	
	cd opus-1.3.1
	./configure \
		CFLAGS='-D_FORTIFY_SOURCE=0' --host=$HOST --prefix=$PREFIX --enable-static \
		--disable-shared --disable-doc --disable-extra-programs
	make -j4 install
	cd ..
fi

# twolame
if [[ ! -f $PREFIX/lib/libtwolame.a ]]; then

        if [[ ! -d twolame ]]; then
                git clone https://github.com/njh/twolame.git
                mv twolame/doc/twolame.1.txt twolame/doc/twolame.1
        fi

        cd twolame
        ./autogen.sh \
        	--host=$HOST --prefix=$PREFIX --enable-static --disable-shared \
        	--disable-maintainer-mode --disable-sndfile
        make -j4 install
        cd ..

fi

# ffmpeg
if [[ ! -f $PREFIX/lib/libavformat.a ]]; then
	
	if [[ ! -d ffmpeg ]]; then
		git clone --depth 1 https://github.com/FFmpeg/FFmpeg.git ffmpeg
	fi
	
	cd ffmpeg
	./configure \
		--enable-gpl --enable-nonfree --enable-libfdk-aac \
		--enable-libopus --enable-static --disable-shared \
		--disable-programs --disable-outdevs --disable-encoders \
		--arch=x86_64 --target-os=mingw64 --cross-prefix=$HOST- \
		--pkg-config=pkg-config --prefix=$PREFIX
	make -j4 install
	cd ..
fi

cd ..
CROSS_HOST=$HOST- make -j4 EXTRA_LDFLAGS="-static" EXTRA_CFLAGS="-DLIBTWOLAME_STATIC" EXTRA_PKGS="libusb-1.0"
$HOST-strip satradio.exe

echo "Done"

