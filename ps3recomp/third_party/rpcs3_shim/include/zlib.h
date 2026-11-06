// R2PORT: minimal zlib API surface used by rpcs3/util/logs.cpp (gzip copy of the log)
// and Crypto/unself.h (SELF decompression, never reached by the RSX).
// The functions are stubbed in rpcs3_shim/misc_stubs.cpp and always fail, so the
// gzip log is simply not produced. Do not use for anything else.
#pragma once

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_NO_FLUSH      0
#define Z_FINISH        4
#define Z_DEFLATED      8
#define Z_DEFAULT_STRATEGY 0

typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef unsigned long uLongf;

typedef struct z_stream_s
{
	const Bytef* next_in;
	uInt avail_in;
	uLong total_in;
	Bytef* next_out;
	uInt avail_out;
	uLong total_out;
	const char* msg;
	void* state;
	void* zalloc;
	void* zfree;
	void* opaque;
	int data_type;
	uLong adler;
	uLong reserved;
} z_stream;

typedef z_stream* z_streamp;

#ifdef __cplusplus
extern "C" {
#endif
int r2port_deflateInit2(z_streamp strm, int level, int method, int windowBits, int memLevel, int strategy);
int r2port_deflate(z_streamp strm, int flush);
int r2port_deflateEnd(z_streamp strm);
int r2port_uncompress(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen);
#ifdef __cplusplus
}
#endif

#define deflateInit2 r2port_deflateInit2
#define deflate r2port_deflate
#define deflateEnd r2port_deflateEnd
#define uncompress r2port_uncompress
