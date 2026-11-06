// Link test for the vendored RPCS3 RSX library. Not meant to run: it only references the
// entry points the port will use so the linker has to resolve them.
#include "stdafx.h"
#include "Emu/IdManager.h"
#include "Emu/System.h"
#include "Emu/emu_callbacks.h"
#include "Emu/system_config.h"
#include "Emu/RSX/RSXThread.h"
#include "Emu/RSX/rsx_methods.h"
#include "Emu/RSX/Core/RSXContext.h"
#include "Emu/RSX/Core/RSXRendererFactory.h"
#include "Emu/RSX/Core/RSXRenderBackend.h"
#include "Emu/RSX/VK/VKGSRender.h"

#include <cstdio>
#include <cstdint>

// Provided by the port runtime in the real game.
extern "C" uint8_t* vm_base = nullptr;
extern "C" uint8_t* vm_sudo_base = nullptr;

namespace rsx
{
	extern std::function<bool(u32 addr, bool is_writing)> g_access_violation_handler;
}

int main(int argc, char** argv)
{
	using create_fn = void (*)(utils::serial*);
	volatile create_fn create = &rsx::create_render_thread;

	using av_fn = bool (rsx::render_backend::*)(rsx::context*, u32, bool);
	volatile av_fn vk_av = &rsx::render_backend::on_access_violation;

	using send_fn = bool (rsx::thread::*)(rsx::context*, u64, u64, u64);
	volatile send_fn send = &rsx::thread::send_event;

	std::printf("rpcs3_rsx linktest: renderer=%d running=%d\n",
		static_cast<int>(g_cfg.video.renderer.get()), Emu.IsRunning() ? 1 : 0);

	// Never true in practice; keeps the calls without executing them.
	if (argc > 1000)
	{
		auto backend = rsx::make_render_backend(video_renderer::vulkan, nullptr, nullptr);
		auto vk = std::make_unique<VKGSRender>(nullptr, nullptr);

		auto regs = std::make_unique<rsx::rsx_state>();
		regs->init();
		regs->decode(0, 0);
		regs->reset();

		if (rsx::g_access_violation_handler)
		{
			rsx::g_access_violation_handler(0, false);
		}

		create(nullptr);

		if (auto rsxthr = g_fxo->try_get<rsx::thread>())
		{
			(rsxthr->*send)(nullptr, 0, 0, 0);
			(vk.get()->*vk_av)(nullptr, 0, false);
		}

		g_emu_callbacks.get_gs_frame = nullptr;
		std::printf("%p %p %s\n", static_cast<void*>(backend.get()), static_cast<void*>(regs.get()), argv[0]);
	}

	return 0;
}
