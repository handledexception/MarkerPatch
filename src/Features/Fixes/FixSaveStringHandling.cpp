#pragma once

#include "../../Globals.cpp"

// =========================
// FixSaveStringHandling
// =========================

safetyhook::InlineHook LoadSaveFileList;
safetyhook::InlineHook CopyStringFromSave;

static int __fastcall LoadSaveFileList_Hook(int thisPtr, int, const wchar_t* Source, int a3, int a4)
{
	const wchar_t* original = Source;

	// Replace wildcard with two-char pattern for save slots
	if (wcscmp(Source, L"ds_slot_*.deadspacesaved") == 0)
	{
		Source = L"ds_slot_??.deadspacesaved";
	}
	else if (wcscmp(Source, L"ds_slot_*.deadspace2saved") == 0)
	{
		Source = L"ds_slot_??.deadspace2saved";
	}

	int result = LoadSaveFileList.thiscall<int>(thisPtr, Source, a3, a4);

	// Records whether the game's pattern was one we rewrite; an unexpected pattern means the fix silently did not apply
	LogHelper::Log("LoadSaveFileList: pattern \"%ls\" -> \"%ls\" (%s), result=%d", original, Source, original == Source ? "passed through" : "rewritten", result);
	return result;
}

static errno_t __cdecl CopyStringFromSave_Hook(wchar_t* Destination, wchar_t* Source)
{
	if (!Source)
	{
		// NULL source
		if (Destination)
		{
			memset(Destination, 0, 0x80);
		}

		return -1;
	}

	if (!Destination)
	{
		return -1;
	}

	size_t i;
	for (i = 0; i < 0x7F; i++)
	{
		if (Source[i] == L'\0')
		{
			Destination[i] = L'\0';
			LogHelper::Log("CopyStringFromSave: \"%ls\" (%zu chars)", Source, i);
			return 0;
		}

		Destination[i] = Source[i];
	}

	Destination[0x7F] = L'\0';
	LogHelper::Log("CopyStringFromSave: TRUNCATED to 127 chars, source was %zu: \"%ls\"", wcslen(Source), Source);
	return 0;
}

// Both functions are byte-identical between the games (verified against the GOG DS1
// build), so the same patterns serve both.
static constexpr GameSig SIG_LoadSaveFileList = {
	"83 EC 18 53 56 57 33 DB 6A 2C 53 8B F1",           // DS1 @ 0x008EC4D0
	"83 EC 18 53 56 57 33 DB 6A 2C 53 8B F1",           // DS2
};
static constexpr GameSig SIG_CopyStringFromSave = {
	"8B 44 24 08 85 C0 74 14 50 8B 44 24 08 68 80 00",  // DS1 @ 0x008EA540
	"8B 44 24 08 85 C0 74 14 50 8B 44 24 08 68 80 00",  // DS2
};

static void ApplyFixSaveStringHandling()
{
	if (!FixSaveStringHandling) return;

	DWORD addr_LoadSaveFileList = ScanModuleSignature(g_State.GameModule, SIG_LoadSaveFileList, "LoadSaveFileList");
	DWORD addr_CopyStringFromSave = ScanModuleSignature(g_State.GameModule, SIG_CopyStringFromSave, "CopyStringFromSave");

	if (addr_LoadSaveFileList == 0 ||
		addr_CopyStringFromSave == 0) {
		return;
	}

	LoadSaveFileList = HookHelper::CreateHook((void*)addr_LoadSaveFileList, &LoadSaveFileList_Hook);
	CopyStringFromSave = HookHelper::CreateHook((void*)addr_CopyStringFromSave, &CopyStringFromSave_Hook);
}
