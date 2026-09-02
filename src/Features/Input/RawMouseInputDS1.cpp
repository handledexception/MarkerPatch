#pragma once

#include "../../Globals.cpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

// =====================
// RawMouseInput (DS1)
// =====================
//
// DS1 reads the mouse through DirectInput and treats it like an analogue stick: the
// per-frame delta is normalised into a +-1 axis (with a dead zone) and then turned into
// a rotation *rate* multiplied by the frame time. Both halves shrink as the frame rate
// rises, so at 480 fps the camera crawls.
//
// DS2's fix reads the raw deltas from WM_INPUT, but that cannot work here: Windows keeps
// one raw-input registration per process per device type, and DirectInput owns the mouse
// one. Registering our own silently hijacked DirectInput's (the game's mouse died until an
// alt-tab made DirectInput re-register, which in turn silenced ours). So instead:
//
//   1. Hook DirectInput8Create (we are its proxy anyway), then CreateDevice on the
//      returned IDirectInput8, to identify the system mouse device.
//   2. Hook that device's GetDeviceState / GetDeviceData and capture the relative X/Y
//      deltas the game itself consumes each frame.
//   3. Hook the message pump (once per frame) to drop stale deltas, and mid-hook the
//      player camera's call to ApplyCameraRotation to overwrite its pitch/yaw arguments
//      with raw-delta-derived values, scaled as DS2's fix does (raw * sensitivity / 625)
//      and honouring Control.MouseSensitivity.
//
// Only the player camera is covered so far; aiming and scripted camera modes still use
// the game's own path. See PLAN.md.

// Message pump 'sub esp,0x1C; push ebx; push esi; mov esi,[PeekMessageA]; push 3'. Called once per frame, cdecl, returns 0 on WM_QUIT.
static constexpr const char* SIG_MessagePump_DS1 = "83 EC 1C 53 56 8B 35 ?? ?? ?? ?? 6A 03";

// UpdatePlayerCamera prologue. The 'call ApplyCameraRotation' for the mouse/stick delta is at +0x871:
//   [esp] = camera angles, [esp+4] = pitch delta, [esp+8] = yaw delta, [esp+0xC] = roll delta, EAX = 0 (mode)
static constexpr const char* SIG_UpdatePlayerCamera_DS1 = "55 8B EC 83 E4 F0 81 EC 14 01 00 00 53 8B 5D 08 8B 43 6C";
static constexpr uintptr_t   DS1_PLAYERCAM_ROTATION_CALL = 0x871;

// Settings loader storing GetSettingFloat("Control.MouseSensitivity"): 'movss [sens], xmm0' at +0x0E, displacement at +0x12
static constexpr const char* SIG_MouseSensStore_DS1 = "D9 05 ?? ?? ?? ?? B8 ?? ?? ?? ?? D9 1C 24 F3 0F 11 05";
static constexpr uintptr_t   DS1_MOUSESENS_DISP = 0x12;

static constexpr float DS1_MOUSE_DIVISOR = 625.0f; // same as DS2's player camera

// IDirectInput8 / IDirectInputDevice8 vtable slots
static constexpr int DI_VT_CREATEDEVICE = 3;
static constexpr int DID_VT_GETDEVICESTATE = 9;
static constexpr int DID_VT_GETDEVICEDATA = 10;

// GUID_SysMouse {6F1D2B60-D5A0-11CF-BFC7-444553540000}, spelled out to avoid dxguid.lib
static constexpr GUID DS1_GUID_SYSMOUSE = { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

safetyhook::InlineHook DirectInput8CreateDS1;
safetyhook::InlineHook DICreateDeviceDS1;
safetyhook::InlineHook DIGetDeviceStateDS1;
safetyhook::InlineHook DIGetDeviceDataDS1;
safetyhook::InlineHook MessagePumpDS1;

static void*  g_ds1MouseDevice = nullptr; // the IDirectInputDevice8 for the system mouse
static float* g_ds1MouseSens = nullptr;

// Diagnostics: DirectInput mouse reads seen, player-camera call site reached, overrides applied
static std::atomic<long> g_ds1MouseReads{ 0 };
static std::atomic<long> g_ds1CameraCalls{ 0 };
static std::atomic<long> g_ds1CameraOverrides{ 0 };

static HRESULT __stdcall DIGetDeviceStateDS1_Hook(void* self, DWORD cbData, LPVOID lpvData)
{
	HRESULT hr = DIGetDeviceStateDS1.unsafe_stdcall<HRESULT>(self, cbData, lpvData);

	// DIMOUSESTATE and DIMOUSESTATE2 both begin with LONG lX, lY (relative deltas since the last read)
	if (SUCCEEDED(hr) && self == g_ds1MouseDevice && lpvData && cbData >= 2 * sizeof(LONG))
	{
		const LONG* axes = reinterpret_cast<const LONG*>(lpvData);
		g_State.rawMouseDeltaX += axes[0];
		g_State.rawMouseDeltaY += axes[1];
		g_ds1MouseReads++;
	}

	return hr;
}

static HRESULT __stdcall DIGetDeviceDataDS1_Hook(void* self, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags)
{
	HRESULT hr = DIGetDeviceDataDS1.unsafe_stdcall<HRESULT>(self, cbObjectData, rgdod, pdwInOut, dwFlags);

	if (SUCCEEDED(hr) && self == g_ds1MouseDevice && rgdod && pdwInOut && !(dwFlags & DIGDD_PEEK))
	{
		for (DWORD i = 0; i < *pdwInOut; i++)
		{
			const DIDEVICEOBJECTDATA* item = reinterpret_cast<const DIDEVICEOBJECTDATA*>(reinterpret_cast<const uint8_t*>(rgdod) + i * cbObjectData);
			if (item->dwOfs == DIMOFS_X) g_State.rawMouseDeltaX += static_cast<LONG>(item->dwData);
			if (item->dwOfs == DIMOFS_Y) g_State.rawMouseDeltaY += static_cast<LONG>(item->dwData);
		}
		g_ds1MouseReads++;
	}

	return hr;
}

static HRESULT __stdcall DICreateDeviceDS1_Hook(void* self, REFGUID rguid, void** lplpDevice, void* pUnkOuter)
{
	HRESULT hr = DICreateDeviceDS1.unsafe_stdcall<HRESULT>(self, &rguid, lplpDevice, pUnkOuter);

	if (SUCCEEDED(hr) && lplpDevice && *lplpDevice && IsEqualGUID(rguid, DS1_GUID_SYSMOUSE))
	{
		g_ds1MouseDevice = *lplpDevice;
		void** vtable = *reinterpret_cast<void***>(g_ds1MouseDevice);

		// The vtable functions are shared by every device of the class, so the hooks
		// filter on the mouse device pointer.
		if (!DIGetDeviceStateDS1)
		{
			DIGetDeviceStateDS1 = HookHelper::CreateHook(vtable[DID_VT_GETDEVICESTATE], &DIGetDeviceStateDS1_Hook);
			DIGetDeviceDataDS1 = HookHelper::CreateHook(vtable[DID_VT_GETDEVICEDATA], &DIGetDeviceDataDS1_Hook);
		}

		LogHelper::Log("  RawMouseInput(DS1): DirectInput mouse device 0x%p, state/data hooks %s", g_ds1MouseDevice, DIGetDeviceStateDS1 ? "installed" : "FAILED");
	}

	return hr;
}

static HRESULT WINAPI DirectInput8CreateDS1_Hook(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
	HRESULT hr = DirectInput8CreateDS1.unsafe_stdcall<HRESULT>(hinst, dwVersion, &riidltf, ppvOut, punkOuter);

	if (SUCCEEDED(hr) && ppvOut && *ppvOut && !DICreateDeviceDS1)
	{
		void** vtable = *reinterpret_cast<void***>(*ppvOut);
		DICreateDeviceDS1 = HookHelper::CreateHook(vtable[DI_VT_CREATEDEVICE], &DICreateDeviceDS1_Hook);
		LogHelper::Log("  RawMouseInput(DS1): IDirectInput8 created, CreateDevice hook %s", DICreateDeviceDS1 ? "installed" : "FAILED");
	}

	return hr;
}

// Installed from DllMain, before the game can create its input devices. In proxy mode the
// real DirectInput8Create is the one LoadProxyLibrary resolved; under an ASI loader it is
// whatever dinput8.dll the loader exposes (its export forwards to the system one).
static void InstallRawMouseInputDS1Early()
{
	void* target = nullptr;
	if (g_State.isUALPresent)
	{
		HMODULE dinput = GetModuleHandleW(L"dinput8.dll");
		if (dinput) target = GetProcAddress(dinput, "DirectInput8Create");
	}
	else
	{
		target = reinterpret_cast<void*>(dinput8.DirectInput8Create);
	}

	if (!target)
	{
		LogHelper::Log("RawMouseInput(DS1): could not resolve DirectInput8Create, mouse capture disabled");
		return;
	}

	DirectInput8CreateDS1 = HookHelper::CreateHook(target, &DirectInput8CreateDS1_Hook);
	LogHelper::Log("RawMouseInput(DS1): DirectInput8Create hooked at 0x%p", target);
}

static uint8_t __cdecl MessagePumpDS1_Hook()
{
	uint8_t result = MessagePumpDS1.unsafe_ccall<uint8_t>();

	// Start each frame from whatever the game read last frame; the camera hook also
	// picks up anything read between here and the camera update, so nothing is lost
	// and nothing accumulates while no camera is consuming it.
	g_State.frameRawX = g_State.rawMouseDeltaX.exchange(0);
	g_State.frameRawY = g_State.rawMouseDeltaY.exchange(0);

	static int frames = 0;
	if (++frames % 4096 == 0)
	{
		LogHelper::Log("  RawMouseInput(DS1): frame %d: DI mouse reads %ld, camera call site reached %ld / overridden %ld, sens %.3f",
			frames, g_ds1MouseReads.load(), g_ds1CameraCalls.load(), g_ds1CameraOverrides.load(), g_ds1MouseSens ? *g_ds1MouseSens : -1.0f);
	}
	return result;
}

static void ApplyRawMouseInputDS1()
{
	DWORD addr_MessagePump = ScanModuleSignature(g_State.GameModule, SIG_MessagePump_DS1, "MessagePump");
	DWORD addr_UpdatePlayerCamera = ScanModuleSignature(g_State.GameModule, SIG_UpdatePlayerCamera_DS1, "UpdatePlayerCamera");
	DWORD addr_MouseSensStore = ScanModuleSignature(g_State.GameModule, SIG_MouseSensStore_DS1, "MouseSensStore");

	if (addr_MessagePump == 0 || addr_UpdatePlayerCamera == 0 || addr_MouseSensStore == 0) return;

	DWORD callSite = addr_UpdatePlayerCamera + DS1_PLAYERCAM_ROTATION_CALL;
	if (MemoryHelper::ReadMemory<uint8_t>(callSite) != 0xE8)
	{
		LogHelper::Log("  RawMouseInput(DS1): unexpected bytes at the camera call site, not applied");
		return;
	}

	g_ds1MouseSens = reinterpret_cast<float*>(MemoryHelper::ReadMemory<DWORD>(addr_MouseSensStore + DS1_MOUSESENS_DISP));
	LogHelper::Log("  RawMouseInput(DS1): sensitivity at 0x%p, rotation call at 0x%08X -> 0x%08X, DirectInput mouse %s",
		g_ds1MouseSens, callSite, MemoryHelper::ResolveRelativeAddress(callSite, 1), g_ds1MouseDevice ? "already created" : "not created yet");

	MessagePumpDS1 = HookHelper::CreateHook((void*)addr_MessagePump, &MessagePumpDS1_Hook);

	// Overwrite the game's frame-rate-dependent pitch/yaw deltas with raw-input ones
	static SafetyHookMid playerRotation{};
	playerRotation = safetyhook::create_mid(callSite,
		[](safetyhook::Context& ctx)
		{
			g_ds1CameraCalls++;

			LONG rawX = g_State.frameRawX + g_State.rawMouseDeltaX.exchange(0);
			LONG rawY = g_State.frameRawY + g_State.rawMouseDeltaY.exchange(0);
			g_State.frameRawX = 0;
			g_State.frameRawY = 0;
			if (rawX == 0 && rawY == 0) return; // no mouse movement: leave stick/game values alone
			g_ds1CameraOverrides++;

			float sens = *g_ds1MouseSens;
			if (sens == 0.0f) sens = 0.005f;

			float yawDelta = (static_cast<float>(rawX) * sens) / -DS1_MOUSE_DIVISOR;
			float pitchDelta = (static_cast<float>(rawY) * sens) / DS1_MOUSE_DIVISOR;

			float* camera = *reinterpret_cast<float**>(ctx.esp);
			float currentPitch = camera[0];
			pitchDelta = std::clamp(currentPitch + pitchDelta, -PITCH_LIMIT_NORMAL, PITCH_LIMIT_NORMAL) - currentPitch;

			*reinterpret_cast<float*>(ctx.esp + 4) = pitchDelta;
			*reinterpret_cast<float*>(ctx.esp + 8) = yawDelta;

			static int logged = 0;
			if (logged < 5)
			{
				logged++;
				LogHelper::Log("  RawMouseInput(DS1): raw (%ld, %ld) -> yaw %.4f pitch %.4f (sens %.3f)", rawX, rawY, yawDelta, pitchDelta, sens);
			}
		}
	);

	LogHelper::Log("  RawMouseInput(DS1): player camera hook installed");
}
