// R2PORT: stand-in for FFmpeg's libavutil/pixfmt.h. RPCS3's RSX only names the two
// formats the NV3089 software scaler uses; values are internal to rpcs3_shim/swscale_shim.cpp.
#pragma once

enum AVPixelFormat
{
	AV_PIX_FMT_NONE = -1,
	AV_PIX_FMT_ARGB = 25,
	AV_PIX_FMT_RGB565BE = 37,
};
