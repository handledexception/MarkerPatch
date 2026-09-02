#pragma once

#include "../../Globals.cpp"

// =========================
// VSyncRefreshRateFix
// =========================

safetyhook::InlineHook SetHz;

static __int16 __cdecl SetHz_Hook(__int16 hz)
{
	MemoryHelper::WriteMemory<float>(g_Addresses.TargetFrameTimeMsPtr, CalculateFpsConstant(hz));
	return SetHz.ccall<__int16>(hz);
}

static constexpr GameSig SIG_fpsLimiter = {
	"55 8B EC 83 E4 F8 83 EC 2C 53 33 DB 38 1D ?? ?? ?? ?? 56 57 0F 84 ?? ?? ?? ?? 83 7D 08 01",  // DS1 @ 0x007C0D30
	"55 8B EC 83 E4 F8 83 EC 20 53 33 DB 56 38",                                                  // DS2
};

// =====================
// Dead Space 1 variant
// =====================
//
// DS1 has the same software frame limiter (a QueryPerformanceCounter spin-wait against
// param_1 * <per-unit frame time constant>, only active when VSync is on), but no SetHz
// function: MSVC 2005 inlined it, so UpdateDisplaySettings stores the refresh rate straight
// into a 16-bit global. Rather than hook a register-convention setter, a mid-hook at the
// limiter's entry keeps the constant in sync with that global on every call, using the
// actual param_1 instead of DS2's assumed 2. See PLAN.md.

// DS1-only: the settings loader storing GetSettingInt("Window.Hz", 60) into its 16-bit
// global. (UpdateDisplaySettings also has an Hz global, but that one is only written when
// display settings are applied from the menu, so it reads 0 at boot.)
//   push 0x3C ; mov eax, "Window.Hz" ; call GetSettingInt ; add esp,4 ; test bl,bl ; mov word ptr [Hz], ax
static constexpr const char* SIG_SettingsHzStore_DS1 = "6A 3C B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 04 84 DB 66 A3";

// 'mov word ptr [Hz], ax': opcode 66 A3 at +0x11, displacement at +0x13
static constexpr uintptr_t DS1_HZ_STORE = 0x11;

// 'fmul dword ptr [frameTimeConstant]' in the limiter: opcode D8 0D at +0x3C, displacement at +0x3E
static constexpr uintptr_t DS1_FRAMETIME_FMUL = 0x3C;

static DWORD g_ds1HzPtr = 0;

static void ApplyVSyncRefreshRateFixDS1(DWORD addr_fpsLimiter)
{
	DWORD addr_HzStore = ScanModuleSignature(g_State.GameModule, SIG_SettingsHzStore_DS1, "SettingsHzStore");
	if (addr_HzStore == 0) return;

	if (MemoryHelper::ReadMemory<uint16_t>(addr_HzStore + DS1_HZ_STORE) != 0xA366)
	{
		LogHelper::Log("  VSyncRefreshRateFix(DS1): unexpected bytes at Hz store, not applied");
		return;
	}
	if (MemoryHelper::ReadMemory<uint16_t>(addr_fpsLimiter + DS1_FRAMETIME_FMUL) != 0x0DD8)
	{
		LogHelper::Log("  VSyncRefreshRateFix(DS1): unexpected bytes at frame-time fmul, not applied");
		return;
	}

	g_ds1HzPtr = MemoryHelper::ReadMemory<DWORD>(addr_HzStore + DS1_HZ_STORE + 2);
	g_Addresses.TargetFrameTimeMsPtr = MemoryHelper::ReadMemory<DWORD>(addr_fpsLimiter + DS1_FRAMETIME_FMUL + 2);
	LogHelper::Log("  VSyncRefreshRateFix(DS1): Hz global 0x%08X, frame-time constant 0x%08X (currently %.3f ms)",
		g_ds1HzPtr, g_Addresses.TargetFrameTimeMsPtr, MemoryHelper::ReadMemory<float>(g_Addresses.TargetFrameTimeMsPtr));

	static SafetyHookMid syncFrameTime{};
	syncFrameTime = safetyhook::create_mid(addr_fpsLimiter,
		[](safetyhook::Context& ctx)
		{
			int units = *reinterpret_cast<int*>(ctx.esp + 4); // param_1
			int hz = *reinterpret_cast<int16_t*>(g_ds1HzPtr);
			if (units <= 0) return;

			// A missing or zero setting would otherwise disable the limiter outright; cap at
			// the display's real refresh rate instead, which is what VSync would give anyway.
			if (hz <= 0) hz = (int)SystemHelper::GetCurrentDisplayFrequency();

			float wanted = (1000.0f / (float)hz) / (float)units;
			float current = MemoryHelper::ReadMemory<float>(g_Addresses.TargetFrameTimeMsPtr);
			if (fabsf(current - wanted) > 0.001f)
			{
				MemoryHelper::WriteMemory<float>(g_Addresses.TargetFrameTimeMsPtr, wanted);
				LogHelper::Log("  VSyncRefreshRateFix(DS1): hz=%d units=%d -> frame-time constant %.3f -> %.3f ms", hz, units, current, wanted);
			}

			// Effective frame pacing, so the cap can be verified from the log alone
			static LARGE_INTEGER freq = {}, last = {};
			static int calls = 0;
			static int reports = 0;
			if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
			LARGE_INTEGER now; QueryPerformanceCounter(&now);
			if (++calls % 256 == 0 && reports < 6)
			{
				if (last.QuadPart)
				{
					double ms = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart / 256.0;
					LogHelper::Log("  VSyncRefreshRateFix(DS1): pacing over 256 frames: %.2f ms/frame = %.1f fps", ms, 1000.0 / ms);
					reports++;
				}
				last = now;
			}
		}
	);
}

static void ApplyVSyncRefreshRateFix()
{
	if (!VSyncRefreshRateFix) return;

	DWORD addr_fpsLimiter = ScanModuleSignature(g_State.GameModule, SIG_fpsLimiter, "fpsLimiter");
	if (addr_fpsLimiter == 0) return;

	if (g_State.game == Game::DS1)
	{
		ApplyVSyncRefreshRateFixDS1(addr_fpsLimiter);
		return;
	}

	DWORD addr_SetHz = ScanModuleSignature(g_State.GameModule, "66 8B 44 24 04 66 A3 ?? ?? ?? ?? C3 CC CC CC CC 66 A1", "SetHz");
	if (addr_SetHz == 0) return;

	SetHz = HookHelper::CreateHook((void*)addr_SetHz, &SetHz_Hook);
	g_Addresses.TargetFrameTimeMsPtr = MemoryHelper::ReadMemory<int>(addr_fpsLimiter + 0x37);
}
