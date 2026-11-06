// R2PORT: minimal software scaler replacing FFmpeg's libswscale for
// rsx::convert_scale_image (NV3089 scaled image blits done on the CPU).
// Only the two formats RPCS3 passes are supported: AV_PIX_FMT_ARGB (bytes A,R,G,B)
// and AV_PIX_FMT_RGB565BE.
#include <libswscale/swscale.h>

#include <algorithm>
#include <cstdint>
#include <new>

struct SwsContext
{
	int src_w, src_h, dst_w, dst_h;
	AVPixelFormat src_fmt, dst_fmt;
	bool bilinear;
};

namespace
{
	struct rgba
	{
		int c[4]; // a, r, g, b
	};

	int bytes_per_pixel(AVPixelFormat fmt)
	{
		return fmt == AV_PIX_FMT_RGB565BE ? 2 : 4;
	}

	rgba load(const unsigned char* p, AVPixelFormat fmt)
	{
		if (fmt == AV_PIX_FMT_RGB565BE)
		{
			const unsigned v = (unsigned{p[0]} << 8) | p[1];
			const int r = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, b = v & 0x1f;
			return {{255, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)}};
		}

		return {{p[0], p[1], p[2], p[3]}};
	}

	void store(unsigned char* p, AVPixelFormat fmt, const rgba& v)
	{
		if (fmt == AV_PIX_FMT_RGB565BE)
		{
			const unsigned r = static_cast<unsigned>(v.c[1]) >> 3, g = static_cast<unsigned>(v.c[2]) >> 2, b = static_cast<unsigned>(v.c[3]) >> 3;
			const unsigned out = (r << 11) | (g << 5) | b;
			p[0] = static_cast<unsigned char>(out >> 8);
			p[1] = static_cast<unsigned char>(out);
			return;
		}

		for (int i = 0; i < 4; i++)
			p[i] = static_cast<unsigned char>(v.c[i]);
	}
}

extern "C" SwsContext* r2port_sws_getContext(int srcW, int srcH, AVPixelFormat srcFormat, int dstW, int dstH, AVPixelFormat dstFormat,
	int flags, SwsFilter*, SwsFilter*, const double*)
{
	if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
		return nullptr;

	const auto supported = [](AVPixelFormat f) { return f == AV_PIX_FMT_ARGB || f == AV_PIX_FMT_RGB565BE; };
	if (!supported(srcFormat) || !supported(dstFormat))
		return nullptr;

	return new (std::nothrow) SwsContext{srcW, srcH, dstW, dstH, srcFormat, dstFormat, (flags & SWS_FAST_BILINEAR) != 0};
}

extern "C" void r2port_sws_freeContext(SwsContext* ctx)
{
	delete ctx;
}

extern "C" int r2port_sws_scale(SwsContext* ctx, const unsigned char* const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH,
	unsigned char* const dst[], const int dstStride[])
{
	if (!ctx || !srcSlice || !srcSlice[0] || !dst || !dst[0] || srcSliceY != 0 || srcSliceH <= 0)
		return 0;

	const int sbpp = bytes_per_pixel(ctx->src_fmt);
	const int dbpp = bytes_per_pixel(ctx->dst_fmt);
	const int src_h = std::min(srcSliceH, ctx->src_h);

	const auto fetch = [&](int x, int y)
	{
		x = std::clamp(x, 0, ctx->src_w - 1);
		y = std::clamp(y, 0, src_h - 1);
		return load(srcSlice[0] + static_cast<std::ptrdiff_t>(y) * srcStride[0] + static_cast<std::ptrdiff_t>(x) * sbpp, ctx->src_fmt);
	};

	for (int y = 0; y < ctx->dst_h; y++)
	{
		unsigned char* row = dst[0] + static_cast<std::ptrdiff_t>(y) * dstStride[0];

		for (int x = 0; x < ctx->dst_w; x++)
		{
			rgba out;

			if (!ctx->bilinear)
			{
				out = fetch(static_cast<int>(static_cast<std::int64_t>(x) * ctx->src_w / ctx->dst_w),
					static_cast<int>(static_cast<std::int64_t>(y) * ctx->src_h / ctx->dst_h));
			}
			else
			{
				// 8.8 fixed point, pixel-centre aligned
				const std::int64_t fx = std::max<std::int64_t>(0, ((2 * x + 1) * std::int64_t{ctx->src_w} * 256) / (2 * ctx->dst_w) - 128);
				const std::int64_t fy = std::max<std::int64_t>(0, ((2 * y + 1) * std::int64_t{ctx->src_h} * 256) / (2 * ctx->dst_h) - 128);
				const int x0 = static_cast<int>(fx >> 8), y0 = static_cast<int>(fy >> 8);
				const int wx = static_cast<int>(fx & 0xff), wy = static_cast<int>(fy & 0xff);
				const rgba p00 = fetch(x0, y0), p10 = fetch(x0 + 1, y0), p01 = fetch(x0, y0 + 1), p11 = fetch(x0 + 1, y0 + 1);

				for (int i = 0; i < 4; i++)
				{
					const int top = p00.c[i] * (256 - wx) + p10.c[i] * wx;
					const int bottom = p01.c[i] * (256 - wx) + p11.c[i] * wx;
					out.c[i] = (top * (256 - wy) + bottom * wy + (1 << 15)) >> 16;
				}
			}

			store(row + static_cast<std::ptrdiff_t>(x) * dbpp, ctx->dst_fmt, out);
		}
	}

	return ctx->dst_h;
}
