#pragma once

#define MINI_CASE_SENSITIVE
#define _USE_MATH_DEFINES
#define NOMINMAX

#include <Windows.h>
#include <d3d9.h>

#include <format>
#include <mutex>
#include <stacktrace>
#include <system_error>
#include <unordered_set>
#include "ini.hpp"
#include "Controller.hpp"
#include "LAAPatcher.hpp"

#include "dllmain.hpp"
#include "helper.cpp"

#include "AchievementOverlay.hpp"

#pragma comment(lib, "SDL3-static.lib")


// Which game this DLL has been loaded into. Detected in DllMain from the executable
// name, falling back to the statically linked Havok version string, which differs
// between the two titles (DS1 = Havok 5.5.0, DS2 = Havok 6.5.0).
enum class Game
{
	Unknown,
	DS1,
	DS2,
};

// A byte pattern per game. nullptr means the feature has no known location in that
// game, and ScanModuleSignature returns 0 for it without raising an error.
struct GameSig
{
	const char* ds1;
	const char* ds2;
};

struct GlobalState
{
	// System initialization
	bool isInit = false;
	HMODULE GameModule = NULL;
	Game game = Game::Unknown;

	bool isUALPresent = false;

	// Display configuration
	int screenWidth = 0;
	int screenHeight = 0;

	// Input configuration
	float mouseSens = 0.0f;
	bool isControllerActive = false;
	bool isXInverted = false;
	bool isYInverted = false;

	// Raw input state
	std::atomic<LONG> rawMouseDeltaX{ 0 };
	std::atomic<LONG> rawMouseDeltaY{ 0 };
	LONG frameRawX = 0;
	LONG frameRawY = 0;

	// Physics
	float frameTimeScale = 0.0f;
	float constraintMass = 0.0f;
	int deathFrameCount = 0;

	// Scripted aiming scene context
	int* momentumAimOuter = nullptr;
	uintptr_t momentumAimData = 0;
	int* boundedAimOuter = nullptr;
	uintptr_t coneAimData = 0;
	int* oscillatingAimOuter = nullptr;

	// Flare fix
	IDirect3DDevice9* device = nullptr;
	bool inFlareDraw = false;
	IDirect3DBaseTexture9* sourceTex = nullptr;
	IDirect3DTexture9* proxyTex = nullptr;
	IDirect3DSurface9* proxySurf = nullptr;
	bool resourcesValid = false;
	bool snapshotValid = false;

	// Misc
	bool isLoadingShopItems = false;
	bool forceCurrentItem = false;
	int16_t currentHeight = 0;
	float resolutionScale = 0.0f;
	float frameTime = 0.0f;
	bool elevatorFixArmed = false;
};

// Global instance
GlobalState g_State;

struct GameAddresses
{
	DWORD DevicePtr = 0;
	DWORD InputManagerPtr = 0;
	DWORD NgGamePlusPtr = 0;
	DWORD LoadedSaveMemoryPtr = 0;
	DWORD TargetFrameTimeMsPtr = 0;
	DWORD EngineFrameTimePtr = 0;
	DWORD UpsideDownYawMin = 0;
	DWORD UpsideDownYawMax = 0;
	DWORD UpsideDownPitchMin = 0;
	DWORD UpsideDownPitchMax = 0;
	DWORD CameraRollToYawCoef = 0;
};

// Memory addresses
GameAddresses g_Addresses;

static constexpr float TARGET_FRAME_TIME = 1.0f / 30.0f;
static constexpr float PITCH_LIMIT_NORMAL = M_PI / 3.0f;
static constexpr float PITCH_LIMIT_AIM_DOWN = -5.0f * M_PI / 12.0f;
static constexpr float DEG2RAD = 0.017453292f;

// =============================
// Ini Variables
// =============================

// Fixes
bool HavokPhysicsFix = false;
bool HighCoreCPUFix = false;
bool VSyncRefreshRateFix = false;
bool FixDifficultyRewards = false;
bool FixSuitIDConflicts = false;
bool FixSaveStringHandling = false;
bool FixAutomaticWeaponFireRate = false;
bool FixSolarArrayElevator = false;
bool FixBlurResolution = false;
bool FixShadowBlur = false;
bool FixFlareArtifacts = false;
bool FixGlassReflections = false;

// General
bool AchievementSupport = false;
bool DisableOnlineFeatures = false;
bool IncreasedEntityPersistence = false;
int IncreasedEntityPersistenceBodies = 0;
int IncreasedEntityPersistenceLimbs = 0;
bool IncreasedDecalPersistence = false;
bool SkipIntro = false;
bool SkipArtificialLoadingDelay = false;
bool LoadASIPlugins = false;
int CheckLAAPatch = 0;

// Display
bool AutoResolution = false;
bool FontScaling = false;
float FontScalingFactor = 0;

// Input
bool RawMouseInput = false;
bool UseSDLControllerInput = false;
bool BlockDirectInputDevices = false;
bool DisableKeyboardHook = false;
bool GyroEnabled = false;
float GyroSensitivity = 0.0f;
float GyroSmoothing = 0.0f;
bool GyroCalibrationPersistence = false;
bool TouchpadEnabled = false;
bool InvertABXYButtons = false;

// Graphics
int MaxAnisotropy = 0;
bool ForceTrilinearFiltering = false;
int DynamicShadowResolution = 0;

// Modding
bool DumpArchiveAssets = false;
bool LoadModFiles = false;

// DLC
bool EnableHazardPack = false;
bool EnableMartialLawPack = false;
bool EnableSupernovaPack = false;
bool EnableSeveredDLC = false;
bool EnableHackerDLC = false;
bool EnableZealotDLC = false;
bool EnableRivetGunDLC = false;

static const char* GameName(Game game)
{
	switch (game)
	{
		case Game::DS1: return "Dead Space";
		case Game::DS2: return "Dead Space 2";
		default: return "Unknown";
	}
}

static Game DetectGame(HMODULE Module)
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(Module, path, MAX_PATH);
	const std::wstring name = std::filesystem::path(path).filename().wstring();

	if (_wcsicmp(name.c_str(), L"deadspace2.exe") == 0) return Game::DS2;
	if (_wcsicmp(name.c_str(), L"dead space.exe") == 0) return Game::DS1;
	if (_wcsicmp(name.c_str(), L"deadspace.exe") == 0) return Game::DS1;

	// Renamed executable: fall back to the Havok version string each game embeds
	if (MemoryHelper::PatternScan(Module, "48 61 76 6F 6B 2D 35 2E 35 2E 30")) return Game::DS1; // "Havok-5.5.0"
	if (MemoryHelper::PatternScan(Module, "48 61 76 6F 6B 2D 36 2E 35 2E 30")) return Game::DS2; // "Havok-6.5.0"

	return Game::Unknown;
}

static DWORD ScanModuleSignature(HMODULE Module, std::string_view Signature, const char* PatchName, int FunctionStartCheckCount = -1, bool ShowError = true)
{
	DWORD Address = MemoryHelper::FindSignatureAddress(Module, Signature, FunctionStartCheckCount);

	LogHelper::Log("  %-36s %s", PatchName, Address ? std::format("0x{:08X}", Address).c_str() : "NOT FOUND");

	if (Address == 0 && ShowError)
	{
		std::string ErrorMessage = "Error: Unable to find signature for patch: ";
		ErrorMessage += PatchName;
		MessageBoxA(NULL, ErrorMessage.c_str(), "MarkerPatch", MB_ICONERROR);
	}

	return Address;
}

// Picks the pattern for the running game. A feature with no pattern for this game
// simply reports "not found" without an error dialog.
static DWORD ScanModuleSignature(HMODULE Module, const GameSig& Signature, const char* PatchName, int FunctionStartCheckCount = -1, bool ShowError = true)
{
	const char* pattern = g_State.game == Game::DS1 ? Signature.ds1 : Signature.ds2;

	if (!pattern)
	{
		LogHelper::Log("  %-36s not supported for %s", PatchName, GameName(g_State.game));
		return 0;
	}

	return ScanModuleSignature(Module, std::string_view(pattern), PatchName, FunctionStartCheckCount, ShowError);
}

static std::filesystem::path BuildLongPath(const std::string& root, const std::string& relativePath)
{
	return std::filesystem::path("\\\\?\\" + root + "\\" + relativePath);
}

static float CalculateFpsConstant(int target_fps)
{
	// Disable limiter for unlimited FPS
	if (target_fps <= 0)
	{
		return 0.0f;
	}

	float frame_time_ms = 1000.0f / (float)target_fps;
	return frame_time_ms / 2.0f; // Divide by 2 since a1 = 2
}

static inline bool MatchId(const DWORD* item, DWORD d0, DWORD d1, DWORD d2, DWORD d3)
{
	return item[0] == d0 && item[1] == d1 && item[2] == d2 && item[3] == d3;
}

static IDirect3DDevice9* GetD3D9Device()
{
	if (g_State.device) return g_State.device;
	IDirect3DDevice9* dev = nullptr;

	__try
	{
		dev = *reinterpret_cast<IDirect3DDevice9**>(g_Addresses.DevicePtr);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return nullptr;
	}

	g_State.device = dev;
	return dev;
}

static bool IsUALPresent()
{
	for (const auto& entry : std::stacktrace::current())
	{
		HMODULE hModule = NULL;
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)entry.native_handle(), &hModule))
		{
			if (GetProcAddress(hModule, "IsUltimateASILoader") != NULL)
				return true;
		}
	}

	return false;
}
