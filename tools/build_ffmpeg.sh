#!/bin/bash
# Builds the FFmpeg libraries the video player links (libavformat / libavcodec / libswscale / libswresample / libavutil) for the Vita.
#
#   tools/build_ffmpeg.sh <ffmpeg source dir> <install prefix> [make command]
#
# The prebuilt vdpm "ffmpeg" package only has audio decoders and MPEG-4 video; this one adds H.264, HEVC, VP8, VP9, MPEG-2, VC-1 / WMV,
# the usual audio codecs and the MKV / MP4 / AVI / ASF / TS / FLV / PS containers. Tested with FFmpeg 9.0.2.
# Needs VITASDK in the environment, GNU make (on Windows: ezwinports' make.exe) and a POSIX shell (Git Bash is enough).
# Without a host C compiler (Windows with only the Vita SDK) set HOST_CC=true: the libraries need no host tools, only the configure checks do.
# CMake finds the result through -DFFMPEG_DIR=<install prefix> (default third_party/ffmpeg-vita).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$1"; PREFIX="$2"; MAKE="${3:-make}"
: "${VITASDK:?set VITASDK}"
cd "$SRC"

# worker threads with a bigger stack (the Vita's default overflows in the H.264 decoder)
python3 "$HERE/ffmpeg_vita_patch.py" "$SRC" || python "$HERE/ffmpeg_vita_patch.py" "$SRC"

VIDEO_DEC=h264,hevc,mpeg4,msmpeg4v1,msmpeg4v2,msmpeg4v3,mpeg2video,mpeg1video,vp8,vp9,vc1,wmv1,wmv2,wmv3,h263,h263p,flv,mjpeg,theora,rv10,rv20,rv30,rv40,cinepak
AUDIO_DEC=aac,aac_latm,ac3,eac3,dca,mp1,mp2,mp3,flac,vorbis,opus,alac,wmav1,wmav2,wmapro,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s8,pcm_u8,pcm_f32le,adpcm_ms,adpcm_ima_wav,adpcm_ima_qt,truehd,mlp,ape,wavpack,tta,cook
SUB_DEC=subrip,srt,ass,ssa,webvtt,mov_text,dvdsub,dvbsub,pgssub,text
DEMUX=matroska,mov,avi,asf,mpegts,mpegps,mpegvideo,flv,ogg,mp3,flac,wav,aac,ac3,eac3,dts,dtshd,h264,hevc,m4v,aiff,ape,wv,tta,srt,ass,webvtt,mpjpeg,rm,truehd,mlp,vc1
PARSERS=h264,hevc,mpeg4video,mpegvideo,mpegaudio,aac,aac_latm,ac3,dca,flac,vorbis,opus,vp8,vp9,vc1,h263,mjpeg,dvdsub,dvbsub
BSFS=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata,vp9_superframe,vp9_superframe_split,mpeg4_unpack_bframes,h264_metadata,null,noise

./configure --prefix="$PREFIX" \
  --enable-cross-compile --cross-prefix="$VITASDK/bin/arm-vita-eabi-" \
  --arch=armv7-a --cpu=cortex-a9 --target-os=none \
  --disable-shared --enable-static --disable-runtime-cpudetect --disable-armv5te --disable-armv6t2 \
  --disable-everything --disable-programs --disable-doc --disable-debug --disable-network \
  --disable-avdevice --disable-avfilter \
  --disable-bzlib --disable-iconv --disable-lzma --disable-sdl2 --disable-securetransport --disable-xlib \
  --enable-zlib --enable-pthreads --enable-neon --enable-optimizations \
  --enable-decoder=$VIDEO_DEC,$AUDIO_DEC,$SUB_DEC \
  --enable-demuxer=$DEMUX \
  --enable-parser=$PARSERS \
  --enable-bsf=$BSFS \
  --enable-protocol=file \
  --enable-swscale --enable-swresample \
  --extra-cflags="-std=gnu11 -O3 -fomit-frame-pointer -ffast-math -D_BSD_SOURCE -D_GNU_SOURCE -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types" \
  --extra-ldflags="-L$VITASDK/arm-vita-eabi/lib" \
  --pkg-config=false --host-cc="${HOST_CC:-gcc}" --host-ld="${HOST_CC:-gcc}"

"$MAKE" -j"${JOBS:-12}"
"$MAKE" install
