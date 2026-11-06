// R2PORT: replacement for rpcs3/Emu/Memory/vm.cpp.
// Guest memory is owned by the port runtime: vm_base is the 4 GB guest mapping and
// vm_sudo_base is a second RW view of the same memory. Local video memory lives at
// guest 0xC0000000 like in RPCS3. Page tracking, allocation and range locking are
// reduced to what the RSX front end and backends need.
#include "stdafx.h"
#include "Emu/Memory/vm.h"
#include "Emu/Memory/vm_locking.h"
#include "Emu/Memory/vm_reservation.h"
#include "Emu/CPU/CPUThread.h"
#include "util/asm.hpp"

#include <thread>

extern "C" uint8_t* vm_base;
extern "C" uint8_t* vm_sudo_base;

namespace vm
{
	// Bound by reference so updates of vm_base/vm_sudo_base after static init are seen.
	u8* const& g_base_addr = vm_base;
	u8* const& g_sudo_addr = vm_sudo_base;

	// No PPU executable mirror or stat area. Chosen far away from any user-mode pointer so
	// the access violation filter in Utilities/Thread.cpp never classifies a fault as "exec".
	u8* const g_exec_addr = reinterpret_cast<u8*>(0xFFFF'F000'0000'0000ull);
	u8* const g_stat_addr = nullptr;
	u8* const g_free_addr = nullptr;

	alignas(4096) u8 g_reservations[65536 / 128 * 64]{};

	constexpr u32 c_guest_limit = 0xF0000000;

	std::array<memory_page, 0x100000000 / 4096> g_pages{};

	// Everything below 0xF0000000 is treated as mapped RW guest memory.
	[[maybe_unused]] static const bool s_pages_initialized = []()
	{
		for (u32 i = 0; i < c_guest_limit / 4096; i++)
		{
			g_pages[i].raw() = page_readable | page_writable | page_allocated;
		}

		return true;
	}();

	std::array<atomic_t<reservation_waiter_t, 128>, 32 * 16 * 2> g_resrv_waiters_count{};

	thread_local atomic_t<cpu_thread*>* g_tls_locked = nullptr;

	atomic_t<u64, 128> g_range_lock_bits[2]{};

	atomic_t<u64> g_shmem[1]{};

	static atomic_t<u64, 128> s_range_lock_pool[64]{};
	static atomic_t<u64> s_range_lock_used{0};

	bool check_addr(u64 addr, u8 flags, u32 size)
	{
		if (size == 0)
		{
			return true;
		}

		if (addr + size > c_guest_limit || addr + size < addr)
		{
			return false;
		}

		for (u64 i = addr / 4096; i <= (addr + size - 1) / 4096; i++)
		{
			if (~g_pages[i] & (flags | page_allocated))
			{
				return false;
			}
		}

		return true;
	}

	bool page_protect(u32 addr, u32 size, u8 flags_test, u8 flags_set, u8 flags_clear)
	{
		if (!size || u64{addr} + size > c_guest_limit)
		{
			return false;
		}

		for (u32 i = addr / 4096; i <= (addr + size - 1) / 4096; i++)
		{
			if (~g_pages[i] & (flags_test | page_allocated))
			{
				return false;
			}
		}

		for (u32 i = addr / 4096; i <= (addr + size - 1) / 4096; i++)
		{
			g_pages[i].atomic_op([&](u8& v)
			{
				v = (v | flags_set) & ~flags_clear;
			});
		}

		return true;
	}

	bool read_string(u32 addr, u32 max_size, std::string& out_string, bool check_pages) noexcept
	{
		out_string.clear();

		if (!g_base_addr)
		{
			return false;
		}

		for (u32 i = 0; i < max_size; i++)
		{
			if (check_pages && !check_addr(u64{addr} + i))
			{
				return false;
			}

			const char c = static_cast<char>(g_base_addr[static_cast<u32>(addr + i)]);

			if (!c)
			{
				return true;
			}

			out_string += c;
		}

		return true;
	}

	bool try_access(u32 addr, void* ptr, u32 size, bool is_write)
	{
		if (!g_sudo_addr || !check_addr(addr, is_write ? page_writable : page_readable, size))
		{
			return false;
		}

		if (is_write)
		{
			std::memcpy(g_sudo_addr + addr, ptr, size);
		}
		else
		{
			std::memcpy(ptr, g_sudo_addr + addr, size);
		}

		return true;
	}

	// Allocation is owned by the port runtime; these report failure (only rsx_replay uses them).
	u32 alloc(u32 /*size*/, memory_location_t /*location*/, u32 /*align*/)
	{
		return 0;
	}

	bool falloc(u32 /*addr*/, u32 /*size*/, memory_location_t /*location*/, const std::shared_ptr<utils::shm>* /*src*/)
	{
		return false;
	}

	u32 dealloc(u32 /*addr*/, memory_location_t /*location*/, const std::shared_ptr<utils::shm>* /*src*/)
	{
		return 0;
	}

	void lock_sudo(u32 /*addr*/, u32 /*size*/)
	{
	}

	std::shared_ptr<block_t> map(u32 /*addr*/, u32 /*size*/, u64 /*flags*/)
	{
		return nullptr;
	}

	std::shared_ptr<block_t> find_map(u32 /*size*/, u32 /*align*/, u64 /*flags*/)
	{
		return nullptr;
	}

	std::pair<std::shared_ptr<block_t>, bool> unmap(u32 /*addr*/, bool /*must_be_empty*/, const std::shared_ptr<block_t>* /*ptr*/)
	{
		return {};
	}

	std::shared_ptr<block_t> get(memory_location_t /*location*/, u32 /*addr*/)
	{
		return nullptr;
	}

	std::shared_ptr<block_t> reserve_map(memory_location_t /*location*/, u32 /*addr*/, u32 /*area_size*/, u64 /*flags*/)
	{
		return nullptr;
	}

	void passive_lock(cpu_thread& /*cpu*/)
	{
	}

	atomic_t<u64, 128>* alloc_range_lock()
	{
		for (u32 i = 0; i < 64; i++)
		{
			if (!s_range_lock_used.bit_test_set(i))
			{
				return &s_range_lock_pool[i];
			}
		}

		fmt::throw_exception("vm::alloc_range_lock(): out of range locks");
	}

	void free_range_lock(atomic_t<u64, 128>* range_lock) noexcept
	{
		const usz index = range_lock - s_range_lock_pool;

		if (index < 64)
		{
			range_lock->release(0);
			s_range_lock_used.bit_test_reset(static_cast<u32>(index));
		}
	}

	void range_lock_internal(atomic_t<u64, 128>* range_lock, u32 begin, u32 size)
	{
		// No writer ever holds g_range_lock_bits in the port: just publish the range.
		range_lock->store(begin | (u64{size} << 32));
	}

	bool temporary_unlock(cpu_thread& cpu) noexcept
	{
		bs_t<cpu_flag> add_state = cpu_flag::wait;

		if (g_tls_locked && g_tls_locked->compare_and_swap_test(&cpu, nullptr))
		{
			add_state += cpu_flag::memory;
		}

		g_tls_locked = nullptr;

		if (add_state - cpu.state)
		{
			cpu.state += add_state;
			return true;
		}

		return false;
	}

	void temporary_unlock() noexcept
	{
		if (auto cpu = get_current_cpu_thread())
		{
			temporary_unlock(*cpu);
		}
	}

	writer_lock::writer_lock() noexcept
		: range_lock(nullptr)
	{
	}

	writer_lock::writer_lock(u32 /*addr*/, atomic_t<u64, 128>* range_lock, u32 /*size*/, u64 /*flags*/) noexcept
		: range_lock(range_lock)
	{
	}

	writer_lock::~writer_lock() noexcept
	{
		if (range_lock)
		{
			range_lock->release(0);
		}
	}

	void reservation_update(u32 addr)
	{
		auto& res = reservation_acquire(addr);
		const u64 old = res.fetch_op([](u64& r)
		{
			r = (r & ~u64{rsrv_lock_mask}) + 128;
		});

		reservation_notifier_notify(addr, old & -128);
	}

	std::pair<bool, u64> try_reservation_update(u32 addr)
	{
		auto& res = reservation_acquire(addr);
		const u64 rtime = res.load();

		if (!(rtime & rsrv_unique_lock) && res.compare_and_swap_test(rtime, (rtime & -128) + 128))
		{
			return {true, rtime};
		}

		return {false, rtime};
	}

	atomic_t<u32>* reservation_notifier_notify(u32 raddr, u64 rtime, bool postpone)
	{
		const auto waiter = reservation_notifier(raddr, rtime);

		if (waiter->load().wait_flag % 2 == 1)
		{
			if (!waiter->fetch_op([](reservation_waiter_t& value)
			{
				if (value.wait_flag % 2 == 1)
				{
					value.wait_flag++;
					return true;
				}

				return false;
			}).second)
			{
				return nullptr;
			}

			if (postpone)
			{
				return utils::bless<atomic_t<u32>>(&waiter->raw().wait_flag);
			}

			utils::bless<atomic_t<u32>>(&waiter->raw().wait_flag)->notify_all();
		}

		return nullptr;
	}

	u64 reservation_lock_internal(u32 /*addr*/, atomic_t<u64>& res)
	{
		for (u64 i = 0;; i++)
		{
			if (u64 rtime = res; !(rtime & 127) && reservation_try_lock(res, rtime)) [[likely]]
			{
				return rtime;
			}

			if (i < 15)
			{
				busy_wait(500);
			}
			else
			{
				std::this_thread::yield();
			}
		}
	}

	void reservation_shared_lock_internal(atomic_t<u64>& res)
	{
		for (u64 i = 0;; i++)
		{
			auto [_oldd, _ok] = res.fetch_op([&](u64& r)
			{
				if (r & rsrv_unique_lock)
				{
					return false;
				}

				r += 1;
				return true;
			});

			if (_ok) [[likely]]
			{
				return;
			}

			if (i < 15)
			{
				busy_wait(500);
			}
			else
			{
				std::this_thread::yield();
			}
		}
	}

	void reservation_op_internal(u32 addr, std::function<bool()> func)
	{
		auto& res = reservation_acquire(addr);
		reservation_shared_lock_internal(res);

		if (func())
		{
			res += 127;
		}
		else
		{
			res -= 1;
		}
	}

	[[noreturn]] void reservation_escape_internal()
	{
		fmt::throw_exception("vm::reservation_escape_internal() is not supported in the R2 port");
	}
}
