// R2PORT: isolated stubs for the lv2 sys_rsx / cellGcmSys side of the RSX.
// In RPCS3 these live in rpcs3/Emu/Cell/lv2/sys_rsx.cpp. The port replaces this file with a
// bridge to its own event queues; keep anything sys_rsx / cellGcmSys related in here only.
#include "stdafx.h"
#include "Emu/RSX/RSXThread.h"
#include "Emu/RSX/Core/RSXContext.h"
#include "Emu/Cell/ErrorCodes.h"
#include "Emu/Cell/lv2/sys_rsx.h"

// Sends RSX driver events (flip, vblank, user commands...) to the context's lv2 event port.
// Without lv2 there is nobody to receive them: report success so the RSX never retries.
bool rsx::thread::send_event(rsx::context* /*ctx*/, u64 /*data1*/, u64 /*event_flags*/, u64 /*data3*/)
{
	return true;
}

// lv2 sys_rsx_context_attribute. The RSX front end calls it for LLE driver requests, e.g.
// package 0xFEF (user command -> event to the gcm handler queue). No-op here.
error_code sys_rsx_context_attribute(u32 /*context_id*/, u32 /*package_id*/, u64 /*a3*/, u64 /*a4*/, u64 /*a5*/, u64 /*a6*/)
{
	return CELL_OK;
}