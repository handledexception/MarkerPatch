#pragma once

#include "../../Globals.cpp"

// ====================
// SkipIntro
// ====================

safetyhook::InlineHook LoadMovie;
safetyhook::InlineHook LoadUIAnimation;

static char __fastcall LoadMovie_Hook(int thisPtr, int, const char* Source, char a3, int a4, int a5, int a6)
{
	// Skip logos video
	if (strstr(Source, "trio_frontend.vp6\x00"))
	{
		Source = "\x00";
		(void)LoadMovie.disable();
	}

	return LoadMovie.thiscall<char>(thisPtr, Source, a3, a4, a5, a6);
}

static int __stdcall LoadUIAnimation_Hook(int id)
{
	// FE66 - skip the animation playing alongside the video
	if (id == 0x46453636)
	{
		(void)LoadUIAnimation.disable();
		return 0;
	}

	return LoadUIAnimation.stdcall<int>(id);
}

// =====================
// Dead Space 1 variant
// =====================
//
// DS1's boot sequence (EA logo, Dead Space logo, "Press Any Key") is not a movie the
// MoviePlayer loads; it is driven by the frontend state machine (global state variable,
// switch in the frontend update function): states 3-5 play the logo assets, 9 waits for
// a key, 0x10 builds the main menu. State 1 already has a shortcut: if the byte at
// frontend+0x58 is set (the game sets it after the intro has played once per session) it
// jumps straight to 0x10. So the whole intro is skipped by setting that byte before
// state 1 runs, and the game lands on the main menu.

// Frontend update: 'push ebp; mov ebp,esp; and esp,-8; sub esp,0x3C; mov eax,[..]'. param_1 (frontend) at [esp+4] on entry.
static constexpr const char* SIG_FrontendUpdate_DS1 = "55 8B EC 83 E4 F8 83 EC 3C A1";

// The menu's Continue handler starts with 'cmp byte ptr [esp+4],0; push esi; mov dword ptr [state], 0x15'.
// It is only used here as an anchor for the state variable's address (displacement at +0x08).
static constexpr const char* SIG_FrontendStateRef_DS1 = "80 7C 24 04 00 56 C7 05 ?? ?? ?? ?? 15 00 00 00";
static constexpr uintptr_t   DS1_STATE_DISP = 0x08;

static constexpr uintptr_t DS1_FRONTEND_INTRO_DONE = 0x58;
static constexpr uint32_t  DS1_STATE_INIT = 1;

static uint32_t* g_ds1FrontendState = nullptr;

static void ApplySkipIntroDS1()
{
	DWORD addr_FrontendUpdate = ScanModuleSignature(g_State.GameModule, SIG_FrontendUpdate_DS1, "FrontendUpdate");
	DWORD addr_StateRef = ScanModuleSignature(g_State.GameModule, SIG_FrontendStateRef_DS1, "FrontendStateRef");
	if (addr_FrontendUpdate == 0 || addr_StateRef == 0) return;

	g_ds1FrontendState = reinterpret_cast<uint32_t*>(MemoryHelper::ReadMemory<DWORD>(addr_StateRef + DS1_STATE_DISP));
	LogHelper::Log("  SkipIntro(DS1): frontend state at 0x%p", g_ds1FrontendState);

	static SafetyHookMid frontendHook{};
	frontendHook = safetyhook::create_mid(addr_FrontendUpdate,
		[](safetyhook::Context& ctx)
		{
			uint8_t* frontend = *reinterpret_cast<uint8_t**>(ctx.esp + 4);
			if (!frontend) return;

			if (*g_ds1FrontendState == DS1_STATE_INIT && frontend[DS1_FRONTEND_INTRO_DONE] == 0)
			{
				frontend[DS1_FRONTEND_INTRO_DONE] = 1;
				LogHelper::Log("  SkipIntro(DS1): marked intro as already played, frontend goes straight to the main menu");
			}
		}
	);
}

static void ApplySkipIntro()
{
	if (!SkipIntro) return;

	if (g_State.game == Game::DS1)
	{
		ApplySkipIntroDS1();
		return;
	}

	DWORD addr_LoadUIAnimation = ScanModuleSignature(g_State.GameModule, "56 8B 74 24 08 56 E8 ?? ?? ?? ?? 56 E8 ?? ?? ?? ?? 8B C8", "LoadUIAnimation");
	DWORD addr_LoadMovie = ScanModuleSignature(g_State.GameModule, "83 EC 10 53 55 56 33 DB 57 8B F1 88 5C 24 13 E8", "LoadMovie");

	if (addr_LoadUIAnimation == 0 ||
		addr_LoadMovie == 0) {
		return;
	}

	LoadUIAnimation = HookHelper::CreateHook((void*)addr_LoadUIAnimation, &LoadUIAnimation_Hook);
	LoadMovie = HookHelper::CreateHook((void*)addr_LoadMovie, &LoadMovie_Hook);
}
