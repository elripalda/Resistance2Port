// R2PORT: stand-in for FFmpeg's libswscale/swscale.h, implemented by
// rpcs3_shim/swscale_shim.cpp (point / bilinear scaling of ARGB and RGB565BE only).
// The functions are renamed so they never collide with a real FFmpeg.
#pragma once

#include "libavutil/pixfmt.h"

#define SWS_FAST_BILINEAR 1
#define SWS_POINT 0x10

struct SwsContext;
struct SwsFilter;

#define sws_getContext r2port_sws_getContext
#define sws_freeContext r2port_sws_freeContext
#define sws_scale r2port_sws_scale

#ifdef __cplusplus
extern "C" {
#endif
struct SwsContext* r2port_sws_getContext(int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat,
	int flags, struct SwsFilter* srcFilter, struct SwsFilter* dstFilter, const double* param);
void r2port_sws_freeContext(struct SwsContext* ctx);
int r2port_sws_scale(struct SwsContext* ctx, const unsigned char* const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH,
	unsigned char* const dst[], const int dstStride[]);
#ifdef __cplusplus
}
#endif
