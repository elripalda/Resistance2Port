#pragma once
// R2PORT: FIFO command-stream desync diagnostics (R2_FIFO_TRACE=1) and the SPU PUT vs atomic FIFO fetch
// interlock (R2_FIFO_SPU_LOCK=1). Implementation: rpcs3_shim/r2_fifo_trace.cpp.
//
// Hooks (each one line, guarded by r2ft::g_on so they cost a predictable branch when off):
//   RSXFIFO.cpp  FIFO_control::fetch_u32  -> fill_begin()/on_fill()  (atomic cache fill window + copy)
//   RSXFIFO.cpp  FIFO_control::read       -> on_cmd()                (every command header the puller takes)
//   RSXFIFO.cpp  FIFO_control::set_get    -> on_set_get()            (JUMP/CALL/RET/recovery/DMA-control moves)
//   RSXThread.cpp thread::recover_fifo    -> on_recover()            (writes logs/fifo_trace/fifo_desync_N.txt)
#include <cstdint>

namespace r2ft
{
	extern bool g_on;

	std::uint64_t fill_begin();
	void on_fill(std::uint64_t t0, std::uint32_t addr, std::uint32_t put, std::uint32_t cache_addr, std::uint32_t cache_size,
		std::uint32_t next_cmd_entry, std::uint32_t ea, const void* cache);
	void on_cmd(std::uint32_t pos, std::uint32_t cmd);
	void on_set_get(std::uint32_t from, std::uint32_t to, std::uint32_t last_cmd);
	void on_recover(std::uint32_t pos, std::uint32_t last_cmd, std::uint32_t restore_point, const char* where, std::uint32_t line);
}
