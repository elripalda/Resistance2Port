// R2PORT: savestate / serialization stubs (rpcs3/Emu/savestate_utils.cpp, sys_mmapper.cpp ...).
// Savestates are not supported in the port: version queries return 0 and compressed
// file handlers fail every operation.
#include "stdafx.h"
#include "util/serialization.hpp"
#include "util/serialization_ext.hpp"
#include "Emu/Cell/lv2/sys_mmapper.h"

extern void using_global_version_serialization() {}
extern s32 get_global_version_serialization_version() { return 0; }

namespace rsx
{
	extern void using_rsx_serialization() {}
	extern s32 get_rsx_serialization_version() { return 0; }
}

bool compressed_serialization_file_handler::handle_file_op(utils::serial& /*ar*/, usz /*pos*/, usz /*size*/, const void* /*data*/)
{
	m_errored = true;
	return false;
}

usz compressed_serialization_file_handler::get_size(const utils::serial& /*ar*/, usz /*recommended*/) const
{
	return 0;
}

void compressed_serialization_file_handler::skip_until(utils::serial& /*ar*/)
{
}

void compressed_serialization_file_handler::finalize(utils::serial& /*ar*/)
{
}

u16 serial_breathe_and_tag(utils::serial& /*ar*/, std::string_view /*name*/, bool /*tag_bit*/)
{
	return 0;
}

namespace stx
{
	u16 serial_breathe_and_tag(utils::serial& ar, std::string_view name, bool tag_bit)
	{
		return ::serial_breathe_and_tag(ar, name, tag_bit);
	}
}

page_fault_notification_entries::page_fault_notification_entries(utils::serial& /*ar*/)
{
}

void page_fault_notification_entries::save(utils::serial& /*ar*/)
{
}
