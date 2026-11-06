// R2PORT: input subsystem stubs (rpcs3/Emu/Io/*, rpcs3/Input/pad_thread.cpp).
// The overlays (home menu, message dialogs, OSK) query pads/keyboards through these.
// There is no pad thread in the port, so overlays receive no input.
#include "stdafx.h"
#include "Emu/Io/interception.h"
#include "Emu/Io/KeyboardHandler.h"
#include "Emu/Io/Keyboard.h"
#include "Input/pad_thread.h"

namespace input
{
	atomic_t<active_mouse_and_keyboard> g_active_mouse_and_keyboard{active_mouse_and_keyboard::pad};
	atomic_t<bool> g_pads_intercepted{false};
	atomic_t<bool> g_keyboards_intercepted{false};
	atomic_t<bool> g_mice_intercepted{false};

	void SetIntercepted(bool pads_intercepted, bool keyboards_intercepted, bool mice_intercepted, const char* /*func*/)
	{
		g_pads_intercepted = pads_intercepted;
		g_keyboards_intercepted = keyboards_intercepted;
		g_mice_intercepted = mice_intercepted;
	}

	void SetIntercepted(bool all_intercepted, const char* func)
	{
		SetIntercepted(all_intercepted, all_intercepted, all_intercepted, func);
	}
}

namespace pad
{
	atomic_t<pad_thread*> g_pad_thread = nullptr;
	shared_mutex g_pad_mutex;
}

template <>
void fmt_class_string<CellKbMappingType>::format(std::string& out, u64 arg)
{
	fmt::append(out, "%d", static_cast<s32>(arg));
}

keyboard_consumer& KeyboardHandlerBase::AddConsumer(keyboard_consumer::identifier id, u32 /*max_connect*/)
{
	auto it = m_consumers.find(id);

	if (it == m_consumers.end())
	{
		it = m_consumers.emplace(id, keyboard_consumer(id)).first;
	}

	return it->second;
}

void KeyboardHandlerBase::RemoveConsumer(keyboard_consumer::identifier id)
{
	m_consumers.erase(id);
}

void KeyboardHandlerBase::save(utils::serial& /*ar*/)
{
}
