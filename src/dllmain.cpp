#pragma once

#include "Globals.cpp"

#include "Features/Fixes/HavokPhysicsFix.cpp"
#include "Features/Fixes/HighCoreCPUFix.cpp"
#include "Features/Fixes/VSyncRefreshRateFix.cpp"
#include "Features/Fixes/FixDifficultyRewards.cpp"
#include "Features/Fixes/FixSuitIDConflicts.cpp"
#include "Features/Fixes/FixSaveStringHandling.cpp"
#include "Features/Fixes/FixAutomaticWeaponFireRate.cpp"
#include "Features/Fixes/FixSolarArrayElevator.cpp"
#include "Features/Fixes/FixBlurResolution.cpp"
#include "Features/Fixes/FixShadowBlur.cpp"
#include "Features/Fixes/FixFlareArtifacts.cpp"
#include "Features/Fixes/FixGlassReflections.cpp"

#include "Features/General/AchievementSupport.cpp"
#include "Features/General/DisableOnlineFeatures.cpp"
#include "Features/General/IncreasedEntityPersistence.cpp"
#include "Features/General/IncreasedDecalPersistence.cpp"
#include "Features/General/SkipIntro.cpp"
#include "Features/General/SkipArtificialLoadingDelay.cpp"
#include "Features/General/ASILoader.cpp"

#include "Features/Display/AutoResolution.cpp"
#include "Features/Display/FontScaling.cpp"

#include "Features/Input/DisableKeyboardHook.cpp"
#include "Features/Input/RawMouseInput.cpp"
#include "Features/Input/FilterInputDevices.cpp"
#include "Features/Input/UseSDLControllerInput.cpp"

#include "Features/Graphics/TextureFiltering.cpp"
#include "Features/Graphics/DynamicShadowResolution.cpp"

#include "Features/Modding/ArchiveDump.cpp"
#include "Features/Modding/ModFiles.cpp"
#include "Features/Modding/ArchiveStreamHook.cpp"

#include "Features/DLC/ShopHooks.cpp"

#include "Features/Misc/MainLoopHook.cpp"
#include "Features/Misc/ResolutionHook.cpp"
#include "Features/Misc/D3D9ApiMidHook.cpp"

#include "Initialization/Initialization.cpp"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		{
			// Prevents DLL from receiving thread notifications
			DisableThreadLibraryCalls(hModule);

			g_State.isUALPresent = IsUALPresent();

			// Skip wrapper initialization when loaded as .asi
			if (!g_State.isUALPresent)
			{
				SystemHelper::LoadProxyLibrary();
			}

			g_State.game = DetectGame(GetModuleHandleA(NULL));

			IniHelper::Init();
			LogHelper::Init(IniHelper::ReadInteger("Debug", "LogFile", 0) == 1);
			LogHelper::Log("MarkerPatch loaded into %s (UAL=%d)", GameName(g_State.game), g_State.isUALPresent);

			if (g_State.game == Game::DS1)
			{
				// The exe imports d3d9.dll, so the loader has normally mapped it before any
				// DllMain runs; load it ourselves only if that assumption fails.
				if (!GetModuleHandleW(L"d3d9.dll"))
				{
					LoadLibraryW(L"d3d9.dll");
				}

				direct3DCreate9Hook = HookHelper::CreateHookAPI(L"d3d9.dll", "Direct3DCreate9", &Direct3DCreate9_Hook);
			}
			else
			{
				// DS2, or an unrecognised executable: the DS2 registry key check inside the
				// hook is the real gate, so an unknown game simply never initialises.
				regOpenKeyHook = HookHelper::CreateHookAPI(L"advapi32.dll", "RegOpenKeyExW", &RegOpenKeyExW_Hook);
			}
			break;
		}
		case DLL_PROCESS_DETACH:
		{
			if (UseSDLControllerInput)
			{
				ControllerHelper::ShutdownSDLGamepad();
			}
			break;
		}
	}
	return TRUE;
}
