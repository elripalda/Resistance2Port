// R2PORT: FIFO command-stream desync diagnostics and the SPU PUT / atomic FIFO fetch interlock.
//
// R2_FIFO_TRACE=1      Keep cheap in-memory rings of what the RSX puller did (command headers, GET moves, atomic
//                      cache fills with a copy of the fetched bytes), of every SPU PUT into IO-mapped memory and of
//                      the HLE GCM writes into the title's command buffer. When rsx::thread::recover_fifo() runs
//                      (invalid method, FIFO_ERROR, dead queue) the rings and the live memory around the bad GET are
//                      written to logs\fifo_trace\fifo_desync_<n>.txt (R2_FIFO_TRACE_DIR overrides the directory,
//                      R2_FIFO_TRACE_MAX the number of dumps per session, default 8).
// R2_FIFO_SPU_LOCK=1   Give SPU PUTs into IO-mapped memory the shared side of the iomap range lock that the atomic
//                      FIFO fetch (FIFO_control::fetch_u32) takes exclusively -- what RPCS3's own SPU DMA does
//                      (SPUThread.cpp do_dma_transfer, rsx::reservation_lock<false, 1>). The port's SPU runtime
//                      copies with a plain memcpy, so without this the puller can cache a command block that an
//                      SPU is still writing.
#include "stdafx.h"
#include "r2_fifo_trace.h"

#include "Emu/RSX/RSXThread.h"
#include "Emu/Cell/lv2/sys_rsx.h"
#include "Emu/Memory/vm.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <Windows.h>

extern "C" void (*g_spu_put_pre_hook)(std::uint32_t ea, std::uint32_t size);
extern "C" void (*g_spu_put_post_hook)(std::uint32_t ea, std::uint32_t size);
extern "C" void (*g_r2ft_guest_hook)(int ev, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e);

namespace r2ft
{
	static bool env_on(const char* name)
	{
		const char* e = std::getenv(name);
		return e && e[0] && e[0] != '0';
	}

	static const bool s_trace = env_on("R2_FIFO_TRACE");
	static const bool s_spu_lock = env_on("R2_FIFO_SPU_LOCK");

	// Gates the RSXFIFO/RSXThread hooks. The SPU lock needs fill_begin() to learn the renderer.
	bool g_on = s_trace || s_spu_lock;

	static std::atomic<rsx::thread*> s_render{nullptr};

	static std::uint64_t s_qpf = []
	{
		LARGE_INTEGER f{};
		QueryPerformanceFrequency(&f);
		return static_cast<std::uint64_t>(f.QuadPart ? f.QuadPart : 1);
	}();

	static inline std::uint64_t now()
	{
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		return static_cast<std::uint64_t>(c.QuadPart);
	}

	static inline double to_us(std::int64_t ticks)
	{
		return static_cast<double>(ticks) * 1e6 / static_cast<double>(s_qpf);
	}

	static inline std::uint32_t bswap(std::uint32_t v)
	{
		return _byteswap_ulong(v);
	}

	// ---- RSX thread timeline (single writer: the RSX thread) ---------------------------------------------------

	enum : std::uint32_t { st_cmd = 1, st_move = 2, st_fill = 3 };

	struct stream_ev
	{
		std::uint32_t type;
		std::uint32_t a; // cmd: pos     move: from          fill: fetch addr
		std::uint32_t b; // cmd: header  move: to            fill: cache_addr
		std::uint32_t c; // cmd: -       move: last header   fill: cache_size
	};

	constexpr std::uint32_t c_stream = 8192;
	static stream_ev s_stream[c_stream];
	static std::uint64_t s_stream_n = 0;

	struct fill_ev
	{
		std::uint64_t t0, t1;
		std::uint32_t addr, put, cache_addr, cache_size, next_cmd, ea, seq;
	};

	constexpr std::uint32_t c_fills = 512;
	static fill_ev s_fills[c_fills];
	static std::uint32_t s_fill_n = 0;

	struct fill_copy
	{
		std::uint32_t seq, cache_addr, size, ea;
		std::uint8_t data[1024];
	};

	constexpr std::uint32_t c_copies = 32;
	static fill_copy s_copies[c_copies];

	// ---- SPU PUTs (many writers) ---------------------------------------------------------------------------------

	struct spu_ev
	{
		std::uint64_t t0, t1;
		std::uint32_t ea, size, tid, seq;
	};

	constexpr std::uint32_t c_spu = 16384;
	static spu_ev s_spu[c_spu];
	static std::atomic<std::uint32_t> s_spu_n{0};

	// ---- HLE GCM writes into the title's command buffer (game thread) --------------------------------------------

	struct guest_ev
	{
		std::uint64_t t;
		int ev;
		std::uint32_t a, b, c, d, e, tid, seq;
	};

	constexpr std::uint32_t c_guest = 512;
	static guest_ev s_guest[c_guest];
	static std::atomic<std::uint32_t> s_guest_n{0};

	static thread_local std::uint64_t tl_put_t0 = 0;
	static thread_local std::uint32_t tl_lock_ea = 0;
	static thread_local std::uint32_t tl_lock_len = 0;

	static bool io_mapped(rsx::thread* r, std::uint32_t ea, std::uint32_t size)
	{
		const std::uint32_t last = ea + (size ? size - 1 : 0);
		for (std::uint32_t p = ea >> 20; p <= (last >> 20); p++)
		{
			if (r->iomap_table.io[p].load() != umax)
			{
				return true;
			}
		}

		return false;
	}

	static void spu_put_pre(std::uint32_t ea, std::uint32_t size)
	{
		if (s_trace)
		{
			tl_put_t0 = now();
		}

		if (s_spu_lock && size > 1)
		{
			if (rsx::thread* r = s_render.load(std::memory_order_acquire); r && io_mapped(r, ea, size))
			{
				r->iomap_table.lock<false, 1>(ea, size, nullptr);
				tl_lock_ea = ea;
				tl_lock_len = size;
			}
		}
	}

	static void spu_put_post(std::uint32_t ea, std::uint32_t size)
	{
		rsx::thread* r = s_render.load(std::memory_order_acquire);

		if (tl_lock_len)
		{
			r->iomap_table.unlock<false, 1>(tl_lock_ea, tl_lock_len);
			tl_lock_len = 0;
		}

		if (s_trace && r && size && io_mapped(r, ea, size))
		{
			const std::uint32_t n = s_spu_n.fetch_add(1, std::memory_order_relaxed);
			spu_ev& e = s_spu[n % c_spu];
			e.t0 = tl_put_t0;
			e.t1 = now();
			e.ea = ea;
			e.size = size;
			e.tid = GetCurrentThreadId();
			e.seq = n + 1;
		}
	}

	static void guest_hook(int ev, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e)
	{
		rsx::thread* r = s_render.load(std::memory_order_acquire);

		if (ev == 4 && r && r->ctrl)
		{
			// Recycle wait done (a = context, b = IO begin): did the RSX really reach begin?
			c = r->ctrl->get.load();
			d = r->ctrl->put.load();
			if (c != b)
			{
				static std::atomic<std::uint32_t> s_warned{0};
				if (s_warned++ < 16)
				{
					std::fprintf(stderr, "[r2-fifo-trace] recycle wait gave up: GET 0x%x never reached begin 0x%x (PUT 0x%x); the title will overwrite unread commands\n", c, b, d);
					rsx_log.error("[r2-fifo-trace] recycle wait gave up: GET 0x%x never reached begin 0x%x (PUT 0x%x)", c, b, d);
				}
				e = 1;
			}
		}

		const std::uint32_t n = s_guest_n.fetch_add(1, std::memory_order_relaxed);
		guest_ev& g = s_guest[n % c_guest];
		g.t = now();
		g.ev = ev;
		g.a = a, g.b = b, g.c = c, g.d = d, g.e = e;
		g.tid = GetCurrentThreadId();
		g.seq = n + 1;
	}

	static const bool s_installed = []
	{
		if (s_trace || s_spu_lock)
		{
			g_spu_put_pre_hook = spu_put_pre;
			g_spu_put_post_hook = spu_put_post;
		}

		if (s_trace)
		{
			g_r2ft_guest_hook = guest_hook;
			std::fprintf(stderr, "[r2-fifo-trace] on: desync dumps go to %s\n",
				std::getenv("R2_FIFO_TRACE_DIR") ? std::getenv("R2_FIFO_TRACE_DIR") : "logs\\fifo_trace");
		}

		if (s_spu_lock)
		{
			std::fprintf(stderr, "[r2-fifo] SPU PUT / FIFO fetch interlock on (R2_FIFO_SPU_LOCK)\n");
		}

		return true;
	}();

	// ---- RSX-side hooks ------------------------------------------------------------------------------------------

	std::uint64_t fill_begin()
	{
		if (!s_render.load(std::memory_order_relaxed)) [[unlikely]]
		{
			s_render.store(rsx::get_current_renderer(), std::memory_order_release);
		}

		return s_trace ? now() : 0;
	}

	static inline void push(std::uint32_t type, std::uint32_t a, std::uint32_t b, std::uint32_t c)
	{
		stream_ev& e = s_stream[s_stream_n++ % c_stream];
		e.type = type, e.a = a, e.b = b, e.c = c;
	}

	void on_fill(std::uint64_t t0, std::uint32_t addr, std::uint32_t put, std::uint32_t cache_addr, std::uint32_t cache_size,
		std::uint32_t next_cmd_entry, std::uint32_t ea, const void* cache)
	{
		if (!s_trace)
		{
			return;
		}

		const std::uint32_t seq = ++s_fill_n;
		fill_ev& f = s_fills[seq % c_fills];
		f.t0 = t0;
		f.t1 = now();
		f.addr = addr, f.put = put, f.cache_addr = cache_addr, f.cache_size = cache_size;
		f.next_cmd = next_cmd_entry, f.ea = ea, f.seq = seq;

		fill_copy& c = s_copies[seq % c_copies];
		c.seq = seq, c.cache_addr = cache_addr, c.ea = ea;
		c.size = cache_size > sizeof(c.data) ? static_cast<std::uint32_t>(sizeof(c.data)) : cache_size;
		std::memcpy(c.data, cache, c.size);

		push(st_fill, addr, cache_addr, cache_size);
	}

	void on_cmd(std::uint32_t pos, std::uint32_t cmd)
	{
		if (s_trace)
		{
			push(st_cmd, pos, cmd, 0);
		}
	}

	void on_set_get(std::uint32_t from, std::uint32_t to, std::uint32_t last_cmd)
	{
		if (s_trace)
		{
			push(st_move, from, to, last_cmd);
		}
	}

	// ---- Dump ----------------------------------------------------------------------------------------------------

	static std::string decode(std::uint32_t cmd)
	{
		char buf[96];
		if ((cmd & 0xe0000003) == 0x20000000)
			std::snprintf(buf, sizeof(buf), "JUMP(old) -> 0x%x", cmd & 0x1ffffffc);
		else if ((cmd & 3) == 1)
			std::snprintf(buf, sizeof(buf), "JUMP -> 0x%x", cmd & 0xfffffffc);
		else if ((cmd & 3) == 2)
			std::snprintf(buf, sizeof(buf), "CALL -> 0x%x", cmd & 0xfffffffc);
		else if ((cmd & 0xffff0003) == 0x00020000)
			std::snprintf(buf, sizeof(buf), "RET");
		else if (cmd & 0xa0030003)
			std::snprintf(buf, sizeof(buf), "INVALID header");
		else
			std::snprintf(buf, sizeof(buf), "method 0x%04x x%u%s", cmd & 0xfffc, (cmd >> 18) & 0x7ff, (cmd & 0x40000000) ? " non-inc" : "");
		return buf;
	}

	static bool is_method(std::uint32_t cmd)
	{
		return !(cmd & 0xa0030003);
	}

	static std::uint32_t live_ea(rsx::thread* r, std::uint32_t io)
	{
		return r ? r->iomap_table.get_addr(io) : umax;
	}

	static bool live_read(rsx::thread* r, std::uint32_t io, std::uint32_t& out)
	{
		const std::uint32_t ea = live_ea(r, io);
		if (ea == umax)
		{
			return false;
		}

		out = vm::read32(ea);
		return true;
	}

	static const char* guest_name(int ev)
	{
		switch (ev)
		{
		case 1: return "emit";
		case 2: return "emit->title callback";
		case 3: return "recycle begin";
		case 4: return "recycle end";
		default: return "?";
		}
	}

	void on_recover(std::uint32_t pos, std::uint32_t last_cmd, std::uint32_t restore_point, const char* where, std::uint32_t line)
	{
		if (!s_trace)
		{
			return;
		}

		static std::uint32_t s_dumps = 0;
		static const std::uint32_t s_max = []
		{
			const char* e = std::getenv("R2_FIFO_TRACE_MAX");
			return e && e[0] ? static_cast<std::uint32_t>(std::strtoul(e, nullptr, 10)) : 8u;
		}();

		static std::uint64_t s_last_dump = 0;
		const std::uint64_t t_now = now();

		// A recovery storm (20 in 2 s -> dead queue) would otherwise use up the dumps on one event
		if (s_dumps >= s_max || (s_last_dump && to_us(static_cast<std::int64_t>(t_now - s_last_dump)) < 3e6))
		{
			return;
		}

		s_last_dump = t_now;
		const std::uint32_t idx = s_dumps++;

		rsx::thread* r = s_render.load(std::memory_order_acquire);
		if (!r)
		{
			r = rsx::get_current_renderer();
		}

		const char* dir_env = std::getenv("R2_FIFO_TRACE_DIR");
		const std::string dir = dir_env && dir_env[0] ? dir_env : "logs\\fifo_trace";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);

		SYSTEMTIME st{};
		GetLocalTime(&st);
		char name[64];
		std::snprintf(name, sizeof(name), "fifo_desync_%u_%02u%02u%02u.txt", idx, st.wHour, st.wMinute, st.wSecond);
		const std::string path = dir + "\\" + name;

		FILE* f = std::fopen(path.c_str(), "w");
		if (!f)
		{
			std::fprintf(stderr, "[r2-fifo-trace] cannot write %s\n", path.c_str());
			return;
		}

		const std::uint32_t put = r && r->ctrl ? static_cast<std::uint32_t>(r->ctrl->put.load()) : 0u;
		const std::uint32_t get = r && r->ctrl ? static_cast<std::uint32_t>(r->ctrl->get.load()) : 0u;
		const std::uint32_t pos_ea = live_ea(r, pos);

		std::fprintf(f, "R2 FIFO desync dump #%u  %04u-%02u-%02u %02u:%02u:%02u.%03u\n", idx, st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
		std::fprintf(f, "recover_fifo called from %s:%u\n", where ? where : "?", line);
		std::fprintf(f, "puller pos 0x%08x (ea 0x%08x)  last header 0x%08x = %s\n", pos, pos_ea, last_cmd, decode(last_cmd).c_str());
		std::fprintf(f, "ctrl PUT 0x%08x  GET 0x%08x  restore point 0x%08x  SPU interlock %s\n\n", put, get,
			restore_point, s_spu_lock ? "ON" : "off");

		// 1. Header chain. The puller decodes header -> count -> next header; a position that is not where the previous
		//    header said the next one is, with no GET move in between, is where the stream was lost.
		std::fprintf(f, "== 1. header chain breaks (oldest first, last %u timeline events) ==\n", c_stream);
		const std::uint64_t n_ev = s_stream_n;
		const std::uint64_t first = n_ev > c_stream ? n_ev - c_stream : 0;
		{
			std::uint32_t expected = umax, prev_pos = 0, prev_cmd = 0, breaks = 0;
			for (std::uint64_t i = first; i < n_ev; i++)
			{
				const stream_ev& e = s_stream[i % c_stream];
				if (e.type == st_move)
				{
					expected = e.b;
				}
				else if (e.type == st_cmd)
				{
					if (expected != umax && e.a != expected && breaks++ < 24)
					{
						std::uint32_t live = 0;
						const bool lv = live_read(r, e.a, live);
						std::fprintf(f, "  BREAK ev#%llu: header taken at 0x%08x, chain expected 0x%08x (prev header 0x%08x @0x%08x = %s)\n",
							static_cast<unsigned long long>(i), e.a, expected, prev_cmd, prev_pos, decode(prev_cmd).c_str());
						std::fprintf(f, "        seen 0x%08x = %s; live memory now %s0x%08x\n", e.b, decode(e.b).c_str(), lv ? "" : "(unmapped) ", live);
					}

					prev_pos = e.a;
					prev_cmd = e.b;
					expected = is_method(e.b) ? e.a + 4 + 4 * ((e.b >> 18) & 0x7ff) : umax;
				}
			}

			if (!breaks)
			{
				std::fprintf(f, "  none in the window: the bad header sits where the previous header's count pointed, i.e. the\n"
					"  bytes there were not a header when they were fetched (stale/torn data, a bad JUMP/CALL target, or a\n"
					"  writer that changed a count after it was fetched).\n");
			}
		}

		// 2. Timeline tail with live-vs-seen comparison
		std::fprintf(f, "\n== 2. last 160 puller events (cmd: header taken; move: GET change; fill: atomic cache fill) ==\n");
		{
			const std::uint64_t from = n_ev > 160 ? n_ev - 160 : 0;
			for (std::uint64_t i = std::max(from, first); i < n_ev; i++)
			{
				const stream_ev& e = s_stream[i % c_stream];
				if (e.type == st_cmd)
				{
					std::uint32_t live = 0;
					const bool lv = live_read(r, e.a, live);
					std::fprintf(f, "  #%llu cmd  @0x%08x 0x%08x %-34s%s\n", static_cast<unsigned long long>(i), e.a, e.b, decode(e.b).c_str(),
						!lv ? "  [unmapped now]" : (live != e.b ? "  [LIVE MEMORY DIFFERS NOW]" : ""));
					if (lv && live != e.b)
					{
						std::fprintf(f, "        live now 0x%08x = %s\n", live, decode(live).c_str());
					}
				}
				else if (e.type == st_move)
				{
					std::fprintf(f, "  #%llu move 0x%08x -> 0x%08x  (after 0x%08x = %s)\n", static_cast<unsigned long long>(i), e.a, e.b, e.c, decode(e.c).c_str());
				}
				else if (e.type == st_fill)
				{
					std::fprintf(f, "  #%llu fill fetch 0x%08x cache [0x%08x, +0x%x)\n", static_cast<unsigned long long>(i), e.a, e.b, e.c);
				}
			}
		}

		// 3. Live memory around the bad position
		std::fprintf(f, "\n== 3. live memory around pos 0x%08x (IO offsets; > marks pos) ==\n", pos);
		for (std::uint32_t io = (pos & ~3u) - 0x200; io != (pos & ~3u) + 0x100; io += 16)
		{
			std::fprintf(f, "  %c%08x:", (pos >= io && pos < io + 16) ? '>' : ' ', io);
			for (std::uint32_t k = 0; k < 16; k += 4)
			{
				std::uint32_t v = 0;
				if (live_read(r, io + k, v))
					std::fprintf(f, " %08x", v);
				else
					std::fprintf(f, " ????????");
			}
			std::fprintf(f, "\n");
		}

		// 4. Cached copies vs memory now
		std::fprintf(f, "\n== 4. recent atomic cache fills: fetched bytes vs memory now ==\n");
		std::uint32_t racy_fills = 0;
		{
			const std::uint32_t nf = s_fill_n;
			for (std::uint32_t s = nf > c_copies ? nf - c_copies + 1 : 1; s <= nf; s++)
			{
				const fill_copy& c = s_copies[s % c_copies];
				const fill_ev& fe = s_fills[s % c_fills];
				if (c.seq != s || fe.seq != s)
				{
					continue;
				}

				const bool covers = pos >= c.cache_addr && pos < c.cache_addr + std::max<std::uint32_t>(c.size, 4) + 0x400;
				std::uint32_t diffs = 0;
				for (std::uint32_t o = 0; o + 4 <= c.size; o += 4)
				{
					std::uint32_t seen;
					std::memcpy(&seen, c.data + o, 4);
					seen = bswap(seen);
					std::uint32_t live = 0;
					if (live_read(r, c.cache_addr + o, live) && live != seen)
					{
						diffs++;
					}
				}

				// SPU PUTs that overlapped this fill in both address and time
				std::uint32_t overlap = 0;
				const std::uint32_t ns = s_spu_n.load();
				for (std::uint32_t k = ns > c_spu ? ns - c_spu : 0; k < ns; k++)
				{
					const spu_ev& p = s_spu[k % c_spu];
					if (p.seq != k + 1)
						continue;
					if (p.ea < fe.ea + fe.cache_size && p.ea + p.size > fe.ea && p.t0 <= fe.t1 && p.t1 >= fe.t0)
						overlap++;
				}

				racy_fills += overlap ? 1 : 0;

				if (!covers && !diffs && !overlap)
				{
					continue;
				}

				std::fprintf(f, "  fill #%u fetch 0x%08x cache [0x%08x,+0x%x) ea 0x%08x put 0x%08x next_cmd 0x%08x took %.1f us, %.0f us before recover%s%s\n",
					s, fe.addr, fe.cache_addr, fe.cache_size, fe.ea, fe.put, fe.next_cmd, to_us(static_cast<std::int64_t>(fe.t1 - fe.t0)),
					to_us(static_cast<std::int64_t>(t_now - fe.t1)), covers ? "  [covers pos]" : "",
					overlap ? "  [RACE: an SPU PUT into this range ran during the fetch]" : "");

				if (diffs)
				{
					std::fprintf(f, "    %u word(s) differ from memory now:\n", diffs);
					std::uint32_t shown = 0;
					for (std::uint32_t o = 0; o + 4 <= c.size && shown < 48; o += 4)
					{
						std::uint32_t seen;
						std::memcpy(&seen, c.data + o, 4);
						seen = bswap(seen);
						std::uint32_t live = 0;
						if (live_read(r, c.cache_addr + o, live) && live != seen)
						{
							std::fprintf(f, "      0x%08x: fetched 0x%08x  now 0x%08x\n", c.cache_addr + o, seen, live);
							shown++;
						}
					}
				}

				if (covers)
				{
					std::fprintf(f, "    fetched bytes:\n");
					for (std::uint32_t o = 0; o < c.size; o += 16)
					{
						std::fprintf(f, "     %c%08x:", (pos >= c.cache_addr + o && pos < c.cache_addr + o + 16) ? '>' : ' ', c.cache_addr + o);
						for (std::uint32_t k = 0; k < 16 && o + k + 4 <= c.size; k += 4)
						{
							std::uint32_t seen;
							std::memcpy(&seen, c.data + o + k, 4);
							std::fprintf(f, " %08x", bswap(seen));
						}
						std::fprintf(f, "\n");
					}
				}
			}
		}

		// 5. SPU PUTs near the bad position
		std::fprintf(f, "\n== 5. SPU PUTs within 8 KB of pos (ea 0x%08x), newest last ==\n", pos_ea);
		if (pos_ea != umax)
		{
			const std::uint32_t ns = s_spu_n.load();
			std::uint32_t shown = 0;
			for (std::uint32_t k = ns > c_spu ? ns - c_spu : 0; k < ns; k++)
			{
				const spu_ev& p = s_spu[k % c_spu];
				if (p.seq != k + 1)
					continue;
				if (p.ea < pos_ea + 0x2000 && p.ea + p.size + 0x2000 > pos_ea)
				{
					if (shown++ < 96)
					{
						std::fprintf(f, "  put ea 0x%08x size 0x%x tid %u  %.0f us before recover, took %.1f us%s\n", p.ea, p.size, p.tid,
							to_us(static_cast<std::int64_t>(t_now - p.t1)), to_us(static_cast<std::int64_t>(p.t1 - p.t0)),
							(p.ea <= pos_ea && p.ea + p.size > pos_ea) ? "  [covers pos]" : "");
					}
				}
			}

			if (!shown)
			{
				std::fprintf(f, "  none in the last %u SPU PUTs (the bytes were written by the PPU or long ago)\n", c_spu);
			}
		}

		// 6. HLE GCM writes into the title's buffer
		std::fprintf(f, "\n== 6. HLE GCM writes into the title's command buffer (newest last) ==\n");
		{
			const std::uint32_t ng = s_guest_n.load();
			for (std::uint32_t k = ng > 64 ? ng - 64 : 0; k < ng; k++)
			{
				const guest_ev& g = s_guest[k % c_guest];
				if (g.seq != k + 1)
					continue;
				std::fprintf(f, "  %-22s tid %u %.0f us ago: a=0x%08x b=0x%08x c=0x%08x d=0x%08x e=0x%x\n", guest_name(g.ev), g.tid,
					to_us(static_cast<std::int64_t>(t_now - g.t)), g.a, g.b, g.c, g.d, g.e);
			}
			std::fprintf(f, "  (emit: ctx, begin, end, current, words | callback: ctx, callback OPD, current, end, words |\n"
				"   recycle begin: ctx, begin EA, current EA, IO begin, PUT | recycle end: ctx, IO begin, GET, PUT, 1 = gave up)\n");
		}

		std::fprintf(f, "\n== verdict hints ==\n");
		if (racy_fills)
			std::fprintf(f, "  %u recent fill(s) overlapped an SPU PUT to the same bytes: torn/stale fetch. Run with R2_FIFO_SPU_LOCK=1.\n", racy_fills);
		std::fprintf(f, "  BREAK entries in section 1 = the puller lost the header chain without a GET move (parser/abort bug).\n"
			"  LIVE MEMORY DIFFERS on headers the puller already took = the buffer was rewritten after the fetch\n"
			"  (normal for ring reuse far behind GET; suspicious within the last few fills = a writer ahead of GET).\n");

		std::fclose(f);

		std::fprintf(stderr, "[r2-fifo-trace] desync at 0x%x (header 0x%08x), dump: %s%s\n", pos, last_cmd, path.c_str(),
			racy_fills ? " [SPU PUT overlapped a fetch]" : "");
		rsx_log.error("[r2-fifo-trace] desync at 0x%x (header 0x%08x), dump: %s", pos, last_cmd, path);
	}
}
