// R2PORT: stubs for small third-party / host pieces the RSX code pulls in.
#include <zlib.h>

extern "C" int r2port_deflateInit2(z_streamp, int, int, int, int, int) { return Z_STREAM_ERROR; }
extern "C" int r2port_deflate(z_streamp, int) { return Z_STREAM_ERROR; }
extern "C" int r2port_deflateEnd(z_streamp) { return Z_OK; }
extern "C" int r2port_uncompress(Bytef*, uLongf*, const Bytef*, uLong) { return Z_DATA_ERROR; }

// ---- ISO loader (rpcs3/Loader/ISO.cpp): no disc images in the port ----
#include "stdafx.h"
#include "Loader/ISO.h"

bool is_iso_file(const std::string& /*path*/, u64* size, bool* is_raw_device)
{
	if (size) *size = 0;
	if (is_raw_device) *is_raw_device = false;
	return false;
}

iso_archive::iso_archive(const std::string& path)
	: m_path(path)
{
}

bool iso_archive::exists(const std::string& /*path*/)
{
	return false;
}

std::unique_ptr<fs::file_base> iso_archive::open(const std::string& /*path*/)
{
	return nullptr;
}
