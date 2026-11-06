// R2PORT: stubs for the Cell (PPU/SPU/lv2/HLE module) subsystems referenced by the RSX
// library, CPUThread.cpp and Utilities/Thread.cpp. None of these run in the port: the guest
// CPU is recompiled natively and lv2 is the port's own HLE. All stubs are no-ops that
// return neutral values.
#include "stdafx.h"
#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/PPUThread.h"
#include "Emu/Cell/SPUThread.h"
#include "Emu/Cell/lv2/sys_sync.h"
#include "Emu/Cell/lv2/sys_event.h"
#include "Emu/Cell/lv2/sys_process.h"
#include "Emu/Cell/Modules/cellSysutil.h"
#include "Emu/Cell/Modules/cellMsgDialog.h"
#include "Emu/CPU/CPUDisAsm.h"
#include "Emu/RSX/RSXDisAsm.h"
#include "Emu/GDB.h"
#include "Emu/Memory/vm.h"

#include <cstring>

LOG_CHANNEL(sys_log, "SYS");

// ---- Error codes (rpcs3/Emu/Cell/ErrorCodes.cpp, lv2.cpp) ----

s32 error_code::error_report(s32 result, const logs::message* channel, const char* fmt, const fmt_type_info* sup, const u64* args)
{
	if (!channel)
	{
		channel = &sys_log.error;
	}

	if (fmt && sup && args)
	{
		std::string msg;
		fmt::raw_append(msg, fmt, sup, args);
		channel->operator()("Error 0x%x%s", static_cast<u32>(result), msg);
	}

	return result;
}

template <>
void fmt_class_string<CellError>::format(std::string& out, u64 arg)
{
	fmt::append(out, "CELL_ERROR(0x%x)", static_cast<u32>(arg));
}

template <>
void fmt_class_string<CellSysutilLicenseArea>::format(std::string& out, u64 arg)
{
	fmt::append(out, "%d", static_cast<s32>(arg));
}

template <>
void fmt_class_string<CellSysutilLang>::format(std::string& out, u64 arg)
{
	fmt::append(out, "%d", static_cast<s32>(arg));
}

template <>
void fmt_class_string<CellMsgDialogError>::format(std::string& out, u64 arg)
{
	fmt::append(out, "0x%x", static_cast<u32>(arg));
}

template <>
void fmt_class_string<spu_block_hash>::format(std::string& out, u64 arg)
{
	fmt::append(out, "%016x", arg);
}

// ---- lv2 scheduler / sync (rpcs3/Emu/Cell/lv2/lv2.cpp) ----

thread_local bool lv2_obj::g_postpone_notify_barrier{};

atomic_t<u32> g_lv2_preempts_taken{0};

bool lv2_obj::sleep(cpu_thread& /*cpu*/, const u64 /*timeout*/)
{
	return true;
}

void lv2_obj::notify_all() noexcept
{
}

bool lv2_obj::wait_timeout(u64 usec, ppu_thread* /*cpu*/, bool /*scale*/, bool /*is_usleep*/)
{
	thread_ctrl::wait_for(usec);
	return true;
}

void lv2_obj::set_yield_frequency(u64 /*freq*/, u64 /*max_allowed_tsx*/)
{
}

CellError lv2_event_queue::send(lv2_event /*event*/, bool* notified_thread, lv2_event_port* /*port*/)
{
	if (notified_thread)
	{
		*notified_thread = false;
	}

	return CELL_ENOTCONN;
}

s32 process_getpid()
{
	return 1;
}

// ---- PPU / SPU threads ----

void ppu_thread::cmd_list(std::initializer_list<cmd64> /*list*/)
{
}

void ppu_thread::dump_all(std::string& ret) const
{
	ret += "PPU thread (not available in the R2 port)\n";
}

void ppu_register_range(u32 /*addr*/, u32 /*size*/)
{
}

atomic_t<u32> spu_thread::g_raw_spu_id[5]{};

bool spu_thread::read_reg(const u32 /*addr*/, u32& value)
{
	value = 0;
	return false;
}

bool spu_thread::write_reg(const u32 /*addr*/, const u32 /*value*/)
{
	return false;
}

using spu_rdata_t = std::byte[128];

void mov_rdata(spu_rdata_t& _dst, const spu_rdata_t& _src)
{
	std::memcpy(_dst, _src, sizeof(spu_rdata_t));
}

bool cmp_rdata(const spu_rdata_t& _lhs, const spu_rdata_t& _rhs)
{
	return std::memcmp(_lhs, _rhs, sizeof(spu_rdata_t)) == 0;
}

// Only RSX threads exist in the port, so always hand out the RSX disassembler (used by
// cpu_thread::dump_all, which dereferences the result).
std::shared_ptr<CPUDisAsm> make_disasm(const cpu_thread* cpu, shared_ptr<cpu_thread> /*handle*/)
{
	return std::make_shared<RSXDisAsm>(cpu_disasm_mode::interpreter, vm::g_sudo_addr, 0, cpu);
}

bool vm::block_t::falloc(u32 /*addr*/, u32 /*size*/, const std::shared_ptr<utils::shm>* /*src*/, u64 /*flags*/)
{
	return false;
}

// ---- GDB server (rpcs3/Emu/GDB.cpp) ----

gdb_thread::gdb_thread() noexcept
{
}

gdb_thread::~gdb_thread()
{
}

void gdb_thread::operator()()
{
}

void gdb_thread::pause_from(cpu_thread* /*t*/)
{
}
