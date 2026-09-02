#pragma once

#include "../../Globals.cpp"

// =====================
// MaxAnisotropy
// =====================

static safetyhook::InlineHook TX_ChangeOptions;

static int __cdecl TX_ChangeOptions_Hook(int a1, int a2)
{
	int result = TX_ChangeOptions.unsafe_ccall<int>(a1, a2);

	// Get current filtering flags
	int* flags_ptr = (int*)(a2 + 16);
	int flags = *flags_ptr;
	int filtering_mode = flags & 0x30000;

	// Anisotropic Filtering
	if (MaxAnisotropy > 0)
	{
		// Upgrade point (0x0) or linear (0x20000) filtering to anisotropic
		if (filtering_mode == 0x00000 || filtering_mode == 0x20000)
		{
			*(unsigned char*)(a2 + 37) = 3;              // Anisotropic mip mode
			*(unsigned char*)(a2 + 39) = MaxAnisotropy;  // Max anisotropy level
			*flags_ptr = (flags & ~0x30000) | 0x20000;   // Ensure linear flag is set
		}
	}

	// Trilinear Filtering
	if (ForceTrilinearFiltering)
	{
		unsigned char mip_filter = *(unsigned char*)(a2 + 36);
		if (mip_filter == 1) // Point mip filtering
		{
			*(unsigned char*)(a2 + 36) = 2; // Upgrade to linear mip filtering
		}
	}

	return result;
}

static constexpr GameSig SIG_TX_ChangeOptions = {
	"53 8B 58 0C 8B CB 8B D3 81 E1 00 00 03 00",        // DS1 @ 0x007D0360
	"8B 44 24 08 53 8B 58 10 8B CB 8B D3 81 E1 00 00",  // DS2
};

// =====================
// Dead Space 1 variant
// =====================
//
// DS1's TX_ChangeOptions is the same function, but it takes the sampler-options struct
// in EAX rather than on the stack, and the struct layout differs (flags +0x0C, mip
// filter +0x1C, min filter +0x1D, mag filter +0x1E, max anisotropy +0x1F). It already
// selects anisotropic filtering when the flags say so; it just hardcodes the max
// anisotropy to 4. So instead of rewriting the result after the call, steer the flags
// on entry and patch the two immediates in place. See PLAN.md section 7a.

static constexpr uintptr_t DS1_OFF_FLAGS = 0x0C;

// 'mov byte ptr [eax+0x1F], 4' in the anisotropic branch: opcode at +0x3E, immediate at +0x41
static constexpr uintptr_t DS1_MAXANISO_OPCODE = 0x3E;
static constexpr uint32_t  DS1_MAXANISO_EXPECTED = 0x041F40C6;

// 'mov byte ptr [eax+0x1C], 1' in the point-filter branch: opcode at +0x24, immediate at +0x27
static constexpr uintptr_t DS1_POINTMIP_OPCODE = 0x24;
static constexpr uint32_t  DS1_POINTMIP_EXPECTED = 0x011C40C6;

static void ApplyTextureFilteringDS1(DWORD addr)
{
	if (MaxAnisotropy > 0)
	{
		if (MemoryHelper::ReadMemory<uint32_t>(addr + DS1_MAXANISO_OPCODE) == DS1_MAXANISO_EXPECTED)
		{
			MemoryHelper::WriteMemory<uint8_t>(addr + DS1_MAXANISO_OPCODE + 3, (uint8_t)MaxAnisotropy);
			LogHelper::Log("  TextureFiltering(DS1): max anisotropy 4 -> %d", MaxAnisotropy);
		}
		else
		{
			LogHelper::Log("  TextureFiltering(DS1): unexpected bytes at max-anisotropy site, not patched");
		}

		// Linear filtering (mode 0) takes the branch that caps anisotropy at 1; redirect it
		// to the anisotropic branch (mode 0x20000), as the DS2 hook does with its flags.
		static SafetyHookMid steerToAnisotropic{};
		steerToAnisotropic = safetyhook::create_mid(addr,
			[](safetyhook::Context& ctx)
			{
				uint32_t* flags = reinterpret_cast<uint32_t*>(ctx.eax + DS1_OFF_FLAGS);
				uint32_t mode = *flags & 0x30000;

				// Histogram of filter modes seen, so a full run shows which samplers the fix
				// can actually affect (only modes 0 and 0x20000 reach the anisotropic branch)
				static uint32_t seen[4] = {};
				static uint32_t total = 0;
				seen[mode >> 16]++;
				total++;
				if (total <= 4 || (total & (total - 1)) == 0) // first few, then every power of two
				{
					LogHelper::Log("  TextureFiltering(DS1): call %u  modes so far: linear(0)=%u point(1)=%u aniso(2)=%u other(3)=%u",
						total, seen[0], seen[1], seen[2], seen[3]);
				}

				if (mode == 0)
				{
					*flags = (*flags & ~0x30000u) | 0x20000;
				}
			}
		);
	}

	if (ForceTrilinearFiltering)
	{
		if (MemoryHelper::ReadMemory<uint32_t>(addr + DS1_POINTMIP_OPCODE) == DS1_POINTMIP_EXPECTED)
		{
			MemoryHelper::WriteMemory<uint8_t>(addr + DS1_POINTMIP_OPCODE + 3, 2); // point mip -> linear mip
			LogHelper::Log("  TextureFiltering(DS1): point-filter branch mip filter point -> linear");
		}
		else
		{
			LogHelper::Log("  TextureFiltering(DS1): unexpected bytes at mip-filter site, not patched");
		}
	}
}

static void ApplyTextureFiltering()
{
	if (MaxAnisotropy == 0 && !ForceTrilinearFiltering) return;

	DWORD addr_InitTextureSampler = ScanModuleSignature(g_State.GameModule, SIG_TX_ChangeOptions, "TX_ChangeOptions");

	if (addr_InitTextureSampler == 0) return;

	if (g_State.game == Game::DS1)
	{
		ApplyTextureFilteringDS1(addr_InitTextureSampler);
		return;
	}

	TX_ChangeOptions = HookHelper::CreateHook((void*)addr_InitTextureSampler, &TX_ChangeOptions_Hook);
}
