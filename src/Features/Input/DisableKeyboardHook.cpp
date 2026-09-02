#pragma once

#include "../../Globals.cpp"

static constexpr GameSig SIG_SetWindowsHook = {
	// DS1 @ 0x00423860. The DS2 pattern also hits DS1 twice, so DS1 gets a longer unique one.
	"83 3D ?? ?? ?? ?? 00 75 ?? E8 ?? ?? ?? ?? 85 C0 7C ?? E8 ?? ?? ?? ?? 6A 00 6A 00 FF 15 ?? ?? ?? ?? 50 68 ?? ?? ?? ?? 6A 0D FF 15 ?? ?? ?? ?? A3 ?? ?? ?? ?? C3",
	"83 3D ?? ?? ?? ?? 00 75 2B E8",
};

// =====================
// Dead Space 1 variant
// =====================
//
// Both games install the WH_KEYBOARD_LL hook from exactly two places: the installer this
// signature finds, and a one-time keyboard-blocking init a little further on. DS2's patch
// writes a 'ret' at the start of both functions, at +0x00 and +0x50. That second offset is
// DS2-specific: in DS1, +0x50 lands mid-instruction inside the *uninstall* routine (see
// PLAN.md section 7a), so the DS1 port patches the two 'call [SetWindowsHookExA]' sites
// themselves. Each 6-byte indirect call becomes 'xor eax,eax; add esp,0x10; nop', which
// leaves the hook handle NULL, keeps the 16 bytes of stdcall arguments balanced, and lets
// the rest of the init logic run untouched.

static constexpr uintptr_t DS1_HOOK_CALL_SITES[] = { 0x29, 0xA9 };
static constexpr uint8_t   DS1_NEUTERED_CALL[6] = { 0x31, 0xC0, 0x83, 0xC4, 0x10, 0x90 };

static void ApplyDisableKeyboardHookDS1(DWORD addr)
{
	for (uintptr_t site : DS1_HOOK_CALL_SITES)
	{
		DWORD call = addr + site;

		// Expect 'FF 15 <SetWindowsHookExA IAT slot>'
		if (MemoryHelper::ReadMemory<uint16_t>(call) != 0x15FF)
		{
			LogHelper::Log("  DisableKeyboardHook(DS1): unexpected bytes at +0x%X, not patched", (unsigned)site);
			continue;
		}

		MemoryHelper::WriteMemoryRaw(call, DS1_NEUTERED_CALL, sizeof(DS1_NEUTERED_CALL));
		LogHelper::Log("  DisableKeyboardHook(DS1): neutered SetWindowsHookExA call at 0x%08X", call);
	}
}

static void ApplyDisableKeyboardHook()
{
	if (!DisableKeyboardHook) return;

	DWORD addr_SetWindowsHook = ScanModuleSignature(g_State.GameModule, SIG_SetWindowsHook, "SetWindowsHook");

	if (addr_SetWindowsHook == 0) return;

	if (g_State.game == Game::DS1)
	{
		ApplyDisableKeyboardHookDS1(addr_SetWindowsHook);
		return;
	}

	MemoryHelper::WriteMemory<uint8_t>(addr_SetWindowsHook, 0xC3);
	MemoryHelper::WriteMemory<uint8_t>(addr_SetWindowsHook + 0x50, 0xC3);
}