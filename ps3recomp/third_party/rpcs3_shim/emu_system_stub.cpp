// R2PORT: replacement for the parts of rpcs3/Emu/System.cpp (and a few Emu-level helpers)
// referenced by the RSX library. The emulator object reports a RUNNING state from
// construction (Emulator::SetTestMode) so the RSX thread loops normally.
#include "stdafx.h"
#include "Emu/System.h"
#include "Emu/system_config_types.h"
#include "Emu/system_utils.hpp"
#include "Emu/cache_utils.hpp"
#include "Emu/games_config.h"
#include "Emu/localized_string.h"
#include "Emu/emu_callbacks.h"
#include "util/fixed_typemap.hpp"
#include "util/video_provider.h"

#include <cstdio>
#include <cstdlib>

LOG_CHANNEL(sys_log, "SYS");

// Real, assignable callback table. The port sets get_gs_frame (and others) before
// creating the render thread.
emu_callbacks g_emu_callbacks{};

// Real g_fxo storage (same size/alignment as rpcs3/Emu/System.cpp).
stx::manual_typemap<void, 0x20'00000, 128> g_fixed_typemap;

thread_local std::string_view g_tls_serialize_name;

atomic_t<recording_mode> g_recording_mode = recording_mode::stopped;

static atomic_t<bool> s_emulator_available{false};

Emulator::Emulator() noexcept
	: m_supported_renderers({video_renderer::vulkan, video_renderer::null})
	, m_default_renderer(video_renderer::vulkan)
{
	SetTestMode();
	s_emulator_available = true;
}

Emulator::~Emulator() noexcept
{
	s_emulator_available = false;
}

bool Emulator::IsAvailable() noexcept
{
	return s_emulator_available.load();
}

// Forwards to g_emu_callbacks.call_from_main_thread when the port provides it, otherwise
// runs the function on the calling thread.
void Emulator::CallFromMainThread(std::function<void()>&& func, atomic_t<u32>* wake_up, bool /*track_emu_state*/, u64 /*stop_ctr*/, std::source_location /*src_loc*/) const
{
	if (g_emu_callbacks.call_from_main_thread)
	{
		g_emu_callbacks.call_from_main_thread(std::move(func), wake_up);
		return;
	}

	func();

	if (wake_up)
	{
		*wake_up = true;
		wake_up->notify_one();
	}
}

void Emulator::BlockingCallFromMainThread(std::function<void()>&& func, bool /*track_emu_state*/, std::source_location src_loc) const
{
	if (!g_emu_callbacks.call_from_main_thread)
	{
		func();
		return;
	}

	atomic_t<u32> wake_up = 0;
	CallFromMainThread(std::move(func), &wake_up, false, umax, src_loc);

	while (!wake_up)
	{
		wake_up.wait(0);
	}
}

// The port owns the emulation lifetime. Pausing is not supported (the RSX only uses it
// around renderer replacement and copes with "not paused").
bool Emulator::Pause(bool /*freeze_emulation*/, bool /*show_resume_message*/)
{
	return false;
}

void Emulator::Resume()
{
}

void Emulator::RunPPU()
{
}

void Emulator::Kill(bool /*allow_autoexit*/, bool /*savestate*/, savestate_stage* /*stage*/)
{
	sys_log.fatal("Emulator::Kill() requested (ignored by the R2 port)");
}

Emulator Emu;

games_config::games_config() = default;
games_config::~games_config() = default;

[[noreturn]] void report_fatal_error(std::string_view text, bool is_html, bool include_help_text);

void report_fatal_error(std::string_view text, bool /*is_html*/, bool /*include_help_text*/)
{
	std::fprintf(stderr, "RPCS3 fatal error: %.*s\n", static_cast<int>(text.size()), text.data());
	std::fflush(stderr);
	std::abort();
}

std::string get_localized_string(localized_string_id id, const char* args)
{
	if (g_emu_callbacks.get_localized_string)
	{
		return g_emu_callbacks.get_localized_string(id, args);
	}

	return args ? std::string(args) : std::string();
}

std::u32string get_localized_u32string(localized_string_id id, const char* args)
{
	if (g_emu_callbacks.get_localized_u32string)
	{
		return g_emu_callbacks.get_localized_u32string(id, args);
	}

	std::u32string result;

	for (const char* p = args; p && *p; p++)
	{
		result += static_cast<char32_t>(static_cast<u8>(*p));
	}

	return result;
}

namespace r2port
{
	// Directory (with trailing slash) used for RPCS3's on-disk shader cache. Empty disables it.
	std::string g_rsx_cache_dir;
}

namespace rpcs3::cache
{
	std::string get_ppu_cache()
	{
		return r2port::g_rsx_cache_dir;
	}
}

namespace rpcs3::utils
{
	std::string get_game_content_path(game_content_type /*type*/)
	{
		return {};
	}
}
