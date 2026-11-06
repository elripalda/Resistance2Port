// r2rsx_bridge: drives RPCS3's RSX core from the recompiled runtime (see r2rsx_bridge.h).
// Built as part of the rpcs3_rsx library (third_party/rpcs3/CMakeLists.txt) so it sees the
// RPCS3 headers and compile flags; the game only links the C API. GPLv2, like RPCS3.
#include "stdafx.h"
#include "Emu/System.h"
#include "Emu/system_config.h"
#include "Emu/emu_callbacks.h"
#include "Emu/Memory/vm.h"
#include "Emu/Cell/lv2/sys_rsx.h"
#include "Emu/RSX/GSFrameBase.h"
#include "Emu/RSX/RSXThread.h"
#include "Emu/RSX/Core/RSXContext.h"
#include "Emu/RSX/Core/RSXRendererFactory.h"
#include "util/logs.hpp"
#include "util/fixed_typemap.hpp"

#include "r2rsx_bridge.h"

#include <windows.h>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

extern "C" uint8_t* vm_base;
extern "C" uint8_t* vm_sudo_base;
extern "C" int (*g_vm_access_violation_hook)(uint32_t guest_addr, int is_write);

typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" ps3_guest_caller_fn g_ps3_guest_caller;

namespace r2port
{
	extern std::string g_rsx_cache_dir;
}

namespace rsx
{
	extern std::function<bool(u32 addr, bool is_writing)> g_access_violation_handler;
}

namespace
{
	int s_enabled = -1;
	bool s_up = false;
	rsx::thread* s_render = nullptr;

	HWND s_hwnd = nullptr;
	std::mutex s_win_mtx;
	std::condition_variable s_win_cv;

	std::unique_ptr<logs::listener> s_log;

	// Guest handlers run here, like on RPCS3's _gcm_intr_thread: in order, off the RSX thread.
	std::mutex s_intr_mtx;
	std::condition_variable s_intr_cv;
	std::deque<std::pair<u32, u32>> s_intr_queue;

	void intr_thread_main()
	{
		for (;;)
		{
			std::pair<u32, u32> call;
			{
				std::unique_lock lock(s_intr_mtx);
				s_intr_cv.wait(lock, [] { return !s_intr_queue.empty(); });
				call = s_intr_queue.front();
				s_intr_queue.pop_front();
			}

			if (g_ps3_guest_caller && call.first)
			{
				g_ps3_guest_caller(call.first, call.second, 0, 0, 0, 0, 0, 0, 0);
			}
		}
	}

	class r2_gs_frame final : public GSFrameBase
	{
	public:
		void close() override {}
		void reset() override {}
		video_renderer renderer() const override { return video_renderer::vulkan; }
		bool shown() override { return true; }
		void hide() override {}
		void show() override {}
		void toggle_fullscreen() override {}
		void delete_context(draw_context_t) override {}
		draw_context_t make_context() override { return nullptr; }
		void set_current(draw_context_t) override {}
		void flip(draw_context_t, bool) override {}

		int client_width() override
		{
			RECT r{};
			return GetClientRect(s_hwnd, &r) ? std::max<int>(1, r.right - r.left) : 1280;
		}

		int client_height() override
		{
			RECT r{};
			return GetClientRect(s_hwnd, &r) ? std::max<int>(1, r.bottom - r.top) : 720;
		}

		f64 client_display_rate() override { return 60.; }
		bool has_alpha() override { return false; }
		display_handle_t handle() const override { return s_hwnd; }
		bool can_consume_frame() const override { return false; }
		void present_frame(std::vector<u8>&&, u32, u32, u32, bool) const override {}
		void take_screenshot(std::vector<u8>&&, u32, u32, bool) override {}
		void update_title(double) override {}
	};

	std::string exe_dir()
	{
		wchar_t buf[MAX_PATH]{};
		GetModuleFileNameW(nullptr, buf, MAX_PATH);
		return std::filesystem::path(buf).parent_path().string();
	}

	// Launcher graphics settings (settings.bat): applied over rpcs3_config.yml. Unset = yml value.
	void apply_launcher_overrides()
	{
		auto env = [](const char* n) -> const char* { const char* e = std::getenv(n); return e && *e ? e : nullptr; };
		if (const char* e = env("RSX_RES_SCALE"))
		{
			const double s = std::atof(e);
			if (s >= 0.25 && s <= 8.0) g_cfg.video.resolution_scale_percent.set(static_cast<u32>(s * 100.0 + 0.5));
		}
		if (const char* e = env("RSX_FILTER"))
		{
			if (!_stricmp(e, "point") || !_stricmp(e, "nearest")) g_cfg.video.output_scaling.set(output_scaling_mode::nearest);
			else if (!_stricmp(e, "fsr")) g_cfg.video.output_scaling.set(output_scaling_mode::fsr);
			else g_cfg.video.output_scaling.set(output_scaling_mode::bilinear);
		}
		if (const char* e = env("RSX_SHARPEN"))
		{
			const double s = std::atof(e);
			if (s >= 0.0 && s <= 1.0) g_cfg.video.rcas_sharpening_intensity.set(static_cast<u32>(s * 100.0 + 0.5));
		}
		if (const char* e = env("RSX_ANISO"))
		{
			const int a = std::atoi(e);
			if (a >= 0 && a <= 16) g_cfg.video.anisotropic_level_override.set(static_cast<u32>(a));
		}
		// FSR 3.1 / DLAA (RSX_FSR=1/2, VK/VKR2Upscaler.cpp) replace the game's 2x MSAA: it needs single-sample scene targets.
		if (const char* e = env("RSX_FSR"); e && std::atoi(e) > 0)
			g_cfg.video.antialiasing_level.set(msaa_level::none);
		// Host VSync (swapchain present mode): 0 off, 1 on, adaptive.
		if (const char* e = env("RSX_VSYNC"))
			g_cfg.video.vsync.set(!_stricmp(e, "adaptive") ? vsync_mode::adaptive : std::atoi(e) > 0 ? vsync_mode::full : vsync_mode::off);
		// RSX_DISPLAY=exclusive: the fullscreen window (rsx_null_backend.c) may take Vulkan exclusive fullscreen.
		if (const char* e = env("RSX_DISPLAY"))
			g_cfg.video.vk.exclusive_fullscreen_mode.set(!_stricmp(e, "exclusive") ? vk_exclusive_fs_mode::enable : vk_exclusive_fs_mode::disable);
		std::printf("[r2rsx] video: scale %u%%, output scaling %d, sharpen %u, aniso %u, vsync %d, exclusive fs %d\n",
			g_cfg.video.resolution_scale_percent.get(), static_cast<int>(g_cfg.video.output_scaling.get()),
			g_cfg.video.rcas_sharpening_intensity.get(), g_cfg.video.anisotropic_level_override.get(),
			static_cast<int>(g_cfg.video.vsync.get()), static_cast<int>(g_cfg.video.vk.exclusive_fullscreen_mode.get()));
	}

	// Shader compilation and logging defaults (env overrides, applied over rpcs3_config.yml).
	//  R2_SHADER_MODE = async (default: async recompiler, draws skipped until ready)
	//                 | interp (async recompiler + shader interpreter until ready; untested) | sync | interpreter
	//  R2_SHADER_CACHE=0 disables the on-disk shader cache (shadercache\rpcs3\shaders_cache).
	void apply_shader_defaults()
	{
		const char* m = std::getenv("R2_SHADER_MODE");
		shader_mode mode = shader_mode::async_recompiler;
		if (m && *m)
		{
			if (!_stricmp(m, "interp")) mode = shader_mode::async_with_interpreter;
			else if (!_stricmp(m, "sync") || !_stricmp(m, "legacy")) mode = shader_mode::recompiler;
			else if (!_stricmp(m, "interpreter")) mode = shader_mode::interpreter_only;
		}
		g_cfg.video.shadermode.set(mode);
		const char* c = std::getenv("R2_SHADER_CACHE");
		g_cfg.video.disable_on_disk_shader_cache.set(c && *c == '0');
		g_cfg.video.log_programs.set(false);
		std::printf("[r2rsx] shaders: mode %d, disk cache %s\n", static_cast<int>(mode),
			g_cfg.video.disable_on_disk_shader_cache.get() ? "off" : "on");
	}

	void load_config()
	{
		// R2_RSX_CONFIG, else rpcs3_config.yml next to the exe or in its parent: an RPCS3 config.yml.
		std::vector<std::string> paths;
		if (const char* e = std::getenv("R2_RSX_CONFIG"); e && *e) paths.emplace_back(e);
		const std::string dir = exe_dir();
		paths.push_back(dir + "\\rpcs3_config.yml");
		paths.push_back(dir + "\\..\\rpcs3_config.yml");

		for (const auto& p : paths)
		{
			std::ifstream f(p, std::ios::binary);
			if (!f) continue;
			// Only the top-level Video: block. The other sections name enums whose
			// formatters live in parts of RPCS3 that are not vendored, and parsing
			// those never terminates (cfg::try_to_enum_value).
			std::string line, video;
			bool in_video = false;
			while (std::getline(f, line))
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (!line.empty() && line[0] != ' ' && line[0] != '#')
					in_video = line.rfind("Video:", 0) == 0;
				if (in_video) video += line + '\n';
			}
			if (video.empty()) continue;
			if (g_cfg.from_string(video))
			{
				std::printf("[r2rsx] config: %s\n", p.c_str());
				break;
			}
			std::printf("[r2rsx] config: failed to parse %s\n", p.c_str());
		}

		g_cfg.video.renderer.set(video_renderer::vulkan);
		g_cfg.misc.use_native_interface.set(false);
		apply_shader_defaults();
		apply_launcher_overrides();
	}

	void commit(u32 ea, u32 size)
	{
		for (u8* base : { vm_base, vm_sudo_base })
		{
			if (base) VirtualAlloc(base + ea, size, MEM_COMMIT, PAGE_READWRITE);
		}
	}

	int access_violation_hook(uint32_t addr, int is_write)
	{
		return rsx::g_access_violation_handler && rsx::g_access_violation_handler(addr, is_write != 0);
	}
}

extern "C" void r2port_rsx_guest_call(u32 opd, u32 arg)
{
	{
		std::lock_guard lock(s_intr_mtx);
		s_intr_queue.emplace_back(opd, arg);
	}
	s_intr_cv.notify_one();
}

extern "C" int r2rsx_enabled(void)
{
	if (s_enabled < 0)
	{
		// RPCS3/Vulkan is the default renderer; R2_RSX=d3d12 (or 0) selects the old D3D12 backend.
		const char* e = std::getenv("R2_RSX");
		s_enabled = !(e && (!_stricmp(e, "d3d12") || !std::strcmp(e, "0")));
	}
	return s_enabled;
}

extern "C" void r2rsx_set_window(void* hwnd, uint32_t, uint32_t)
{
	{
		std::lock_guard lock(s_win_mtx);
		s_hwnd = static_cast<HWND>(hwnd);
	}
	s_win_cv.notify_all();
}

extern "C" int r2rsx_context_allocate(uint32_t dma_control_ea, uint32_t label_ea,
                                      uint32_t driver_info_ea, uint32_t device_ea,
                                      uint32_t local_size)
{
	if (!r2rsx_enabled()) return -1;
	if (s_up) return 0;

	{
		std::unique_lock lock(s_win_mtx);
		if (!s_win_cv.wait_for(lock, std::chrono::seconds(30), [] { return s_hwnd != nullptr; }))
		{
			std::printf("[r2rsx] no window after 30 s\n");
			return -1;
		}
	}

	const std::string root = exe_dir() + "\\..";
	std::error_code ec;
	std::filesystem::create_directories(root + "\\logs", ec);
	s_log = logs::make_file_listener(root + "\\logs\\rpcs3_rsx.log", 256ull << 20);
	{
		// RSX log verbosity: errors only unless R2_DIAG=1 (RPCS3's notice level) or R2_RSX_LOG=<level>.
		const char* d = std::getenv("R2_DIAG");
		logs::level lv = d && *d && *d != '0' ? logs::level::notice : logs::level::error;
		if (const char* e = std::getenv("R2_RSX_LOG"); e && *e)
		{
			if (!_stricmp(e, "fatal")) lv = logs::level::fatal;
			else if (!_stricmp(e, "error")) lv = logs::level::error;
			else if (!_stricmp(e, "warning")) lv = logs::level::warning;
			else if (!_stricmp(e, "notice")) lv = logs::level::notice;
			else if (!_stricmp(e, "trace")) lv = logs::level::trace;
		}
		logs::set_level(".*", lv);
	}

	// Shader cache next to the other one in shadercache\ (run.bat backs that folder up).
	r2port::g_rsx_cache_dir = root + "\\shadercache\\rpcs3\\";
	if (!std::filesystem::exists(r2port::g_rsx_cache_dir, ec) && std::filesystem::exists(root + "\\cache\\rpcs3\\shaders_cache", ec))
	{
		std::filesystem::create_directories(root + "\\shadercache", ec);
		std::filesystem::rename(root + "\\cache\\rpcs3", r2port::g_rsx_cache_dir.substr(0, r2port::g_rsx_cache_dir.size() - 1), ec);
	}
	std::filesystem::create_directories(r2port::g_rsx_cache_dir, ec);

	load_config();

	g_emu_callbacks.get_gs_frame = []() -> std::unique_ptr<GSFrameBase> { return std::make_unique<r2_gs_frame>(); };
	// Headless: RPCS3 UI hooks must exist (empty std::function throws bad_function_call)
	g_emu_callbacks.get_msg_dialog = []() -> std::shared_ptr<MsgDialogBase> { return nullptr; };
	g_emu_callbacks.get_localized_string = [](auto, const char* s) -> std::string { return s ? s : ""; };
	g_emu_callbacks.get_localized_u32string = [](auto, const char*) -> std::u32string { return {}; };
	g_emu_callbacks.play_sound = [](const std::string&, std::optional<f32>) {};
	g_emu_callbacks.call_from_main_thread = [](std::function<void()> f, atomic_t<u32>* wake) { if (f) f(); if (wake) { *wake = 1; wake->notify_one(); } };

	// Local memory, the RsxReports block, driver info and the device page.
	commit(rsx::constants::local_mem_base, local_size);
	commit(label_ea & ~0xFFFu, 0x10000);
	commit(driver_info_ea & ~0xFFFu, 0x2000);
	commit(device_ea & ~0xFFFu, 0x1000);
	commit(dma_control_ea & ~0xFFFu, 0x1000);

	g_fxo->init();
	rsx::create_render_thread(nullptr);

	s_render = rsx::get_current_renderer();
	if (!s_render)
	{
		std::printf("[r2rsx] render thread was not created\n");
		return -1;
	}

	g_vm_access_violation_hook = access_violation_hook;

	auto* render = s_render;
	render->isHLE = true;
	render->local_mem_size = local_size;
	render->device_addr = device_ea;

	// sys_rsx_context_allocate
	auto& reports = *vm::_ptr<RsxReports>(label_ea);
	std::memset(&reports, 0, sizeof(RsxReports));
	for (auto& n : reports.notify) n.timestamp = -1;
	for (usz i = 0; i < std::size(reports.semaphore); i += 4)
	{
		reports.semaphore[i + 0] = 0x1337C0D3;
		reports.semaphore[i + 1] = 0x1337BABE;
		reports.semaphore[i + 2] = 0x1337BEEF;
		reports.semaphore[i + 3] = 0x1337F001;
	}
	for (auto& r : reports.report)
	{
		r.val = 0;
		r.timestamp = -1;
		r.pad = -1;
	}

	auto& driver = *vm::_ptr<RsxDriverInfo>(driver_info_ea);
	std::memset(&driver, 0, sizeof(RsxDriverInfo));
	driver.version_driver = 0x211;
	driver.version_gpu = 0x5c;
	driver.memory_size = local_size;
	driver.nvcore_frequency = 500000000;
	driver.memory_frequency = 650000000;
	driver.reportsNotifyOffset = 0x1000;
	driver.reportsOffset = 0;
	driver.reportsReportOffset = 0x1400;
	driver.hardware_channel = 1;

	auto& dma = *vm::_ptr<RsxDmaControl>(dma_control_ea);
	dma.get = 0;
	dma.put = 0;
	dma.ref = 0;

	rsx::context* ctx = render->get_context();
	ctx->fw.driver_info = driver_info_ea;
	ctx->fw.main_mem_size = 0x10000000;
	ctx->fw.label_addr = label_ea;
	ctx->display_buffers_count = 0;
	ctx->current_display_buffer = 0;

	render->init_context(ctx, dma_control_ea);

	// Vulkan device and backend resources
	while (!render->is_initialized)
	{
		Sleep(5);
	}

	std::thread(intr_thread_main).detach();

	render->start_device();
	s_up = true;

	std::printf("[r2rsx] RPCS3 RSX up (dma=0x%08X labels=0x%08X driver=0x%08X device=0x%08X local=0x%X)\n",
		dma_control_ea, label_ea, driver_info_ea, device_ea, local_size);
	return 0;
}

extern "C" void r2rsx_iomap(uint32_t ea, uint32_t io, uint32_t size)
{
	if (!s_up) return;
	auto& table = s_render->iomap_table;
	std::lock_guard lock(s_render->sys_rsx_mtx);

	ea >>= 20, io >>= 20, size >>= 20;
	for (u32 i = 0; i < size; i++)
	{
		const u32 prev_ea = table.ea[io + i];
		table.ea[io + i].release((ea + i) << 20);
		if (prev_ea + 1) table.io[prev_ea >> 20].release(-1);
		table.io[ea + i].release((io + i) << 20);
	}
}

extern "C" void r2rsx_iounmap(uint32_t io, uint32_t size)
{
	if (!s_up) return;
	auto& table = s_render->iomap_table;
	std::lock_guard lock(s_render->sys_rsx_mtx);

	for (const u32 end = (io >>= 20) + (size >>= 20); io < end;)
	{
		const u32 ea_entry = table.ea[io];
		table.ea[io++].release(-1);
		if (ea_entry + 1) table.io[ea_entry >> 20].release(-1);
	}
}

extern "C" void r2rsx_set_display_buffer(uint32_t id, uint32_t offset, uint32_t pitch,
                                         uint32_t width, uint32_t height)
{
	if (!s_up || id >= 8) return;
	rsx::context* ctx = s_render->get_context();
	auto& buf = ctx->display_buffers[id];
	buf.offset = offset;
	buf.pitch = pitch;
	buf.width = width;
	buf.height = height;
	if (ctx->display_buffers_count < id + 1) ctx->display_buffers_count = id + 1;
}

extern "C" void r2rsx_set_flip_mode(uint32_t mode)
{
	if (s_up) s_render->requested_vsync = (mode == 1);
}

extern "C" uint32_t r2rsx_get_flip_status(void)
{
	return s_up ? s_render->display_ctrl().get_flip_status() : 0;
}

extern "C" void r2rsx_set_flip_status(uint32_t status)
{
	if (s_up) s_render->display_ctrl().set_flip_status(status);
}

extern "C" uint64_t r2rsx_last_flip_time(void)
{
	return s_up ? s_render->display_ctrl().get_last_guest_flip_timestamp() : 0;
}

extern "C" uint64_t r2rsx_vblank_count(void)
{
	return s_up ? s_render->display_ctrl().get_vblank_count() : 0;
}

extern "C" uint32_t r2rsx_current_display_buffer(void)
{
	return s_up ? s_render->get_context()->current_display_buffer : 0;
}

extern "C" void r2rsx_set_flip_handler(uint32_t opd)   { if (s_up) s_render->flip_handler.set(opd); }
extern "C" void r2rsx_set_vblank_handler(uint32_t opd) { if (s_up) s_render->vblank_handler.set(opd); }
extern "C" void r2rsx_set_user_handler(uint32_t opd)   { if (s_up) s_render->user_handler.set(opd); }
extern "C" void r2rsx_set_queue_handler(uint32_t opd)  { if (s_up) s_render->queue_handler.set(opd); }

extern "C" void r2rsx_set_tile(uint32_t index, uint32_t location, uint32_t offset, uint32_t size,
                               uint32_t pitch, uint32_t comp, uint32_t base, uint32_t bank, int bound)
{
	if (!s_up || index >= std::size(s_render->tiles)) return;
	auto& tile = s_render->tiles[index];

	if (tile.bound && !bound)
	{
		s_render->notify_tile_unbound(index);
	}

	tile.location = location;
	tile.offset = offset;
	tile.size = size;
	tile.pitch = pitch;
	tile.comp = comp;
	tile.base = base;
	tile.bank = bank;
	tile.bound = bound != 0;
}

extern "C" void r2rsx_set_zcull(uint32_t index, uint32_t offset, uint32_t width, uint32_t height,
                                uint32_t cullStart, uint32_t zFormat, uint32_t aaFormat,
                                uint32_t zcullDir, uint32_t zcullFormat, uint32_t sFunc,
                                uint32_t sRef, uint32_t sMask, int bound)
{
	if (!s_up || index >= std::size(s_render->zculls)) return;
	auto& z = s_render->zculls[index];
	z.offset = offset;
	z.width = width;
	z.height = height;
	z.cullStart = cullStart;
	z.zFormat = zFormat;
	z.aaFormat = aaFormat;
	z.zcullDir = zcullDir;
	z.zcullFormat = zcullFormat;
	z.sFunc = sFunc;
	z.sRef = sRef;
	z.sMask = sMask;
	z.bound = bound != 0;
}

extern "C" void r2rsx_wait_get(uint32_t put, unsigned max_ms)
{
	if (!s_up || !s_render->ctrl) return;
	const u64 deadline = GetTickCount64() + max_ms;
	while (s_render->ctrl->get != put && GetTickCount64() < deadline)
	{
		Sleep(0);
	}
}
