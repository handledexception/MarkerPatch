# MarkerPatch — Dead Space 1 Port Plan

Working document for extending MarkerPatch to also patch **Dead Space (2008)**, ideally as a single DLL that detects and patches either game.

**Status:** Phase 0 (recon) complete. **DS1 boot slice working** on branch `ds1-port` —
MarkerPatch loads into DS1, detects the game, fires its init trigger, and applies
`FixSaveStringHandling` with both signatures resolving to the statically predicted
addresses (see §8a).
**Last updated:** 2026-09-01

---

## 1. Executive summary

Dead Space 1 exhibits the *same* three signature defects MarkerPatch fixes in DS2 — built-in VSync locking to 30 FPS, framerate-dependent mouse acceleration, and ragdoll physics destabilising above ~60 FPS. This is shared engine lineage, not coincidence, and it makes the port conceptually straightforward.

The work is not conceptual, it is **address re-derivation**: all 80 byte signatures in this repo target DS2. Ten of them also match DS1's binary; six survive verification.

Key enabling facts established so far:

- DS1 imports `DINPUT8.dll` → the existing dinput8 proxy injection vector works unchanged.
- DS1 is DRM-free (GOG) and unpacked → static analysis works directly.
- The DS2 binary analysed is the **XTEAther-cleaned** executable — TAGES Solidshield DRM stripped, XTEA-encrypted sections decrypted, virtualised functions restored — so static analysis works on it directly. A **stock** Steam `deadspace2.exe` is encrypted and must be cleaned with XTEAther first.
- Both games statically link Havok, but **different major versions** (5.5 vs 6.5).
- DS1 archives use the **same SHOC/SHDR chunk format** the modding hooks already parse.

---

## 2. Binary facts

| | Dead Space (2008) | Dead Space 2 (2011) |
|---|---|---|
| Path analysed | `D:\GOG\Dead Space\Dead Space.exe` | `D:\SteamLibrary\steamapps\common\Dead Space 2\deadspace2.exe` |
| Distribution | GOG (DRM-free) | Steam, TAGES Solidshield DRM — analysed copy is **XTEAther-cleaned**; stock exe preserved as `deadspace2.exe.bak` (48,444,416 bytes) |
| Size | 11,083,776 bytes | 32,001,536 bytes |
| Machine | x86 (`14C`), 32-bit | x86 (`14C`), 32-bit |
| Linker | 8.00 (MSVC 2005) | 9.00 (MSVC 2008) |
| Timestamp | `48DBCF01` → 2008-09-25 10:48:49 | `0` (PE header rewritten by DRM/XTEAther) |
| Image base | `0x400000` | `0x400000` |
| Size of image | `0xB72000` | `0x1FC5000` |
| Size of code | `0x7C8000` | — |
| Entry point | `0x008FC3ED` | — |
| Characteristics | `0x103` (relocs stripped, 32-bit) | `0x102` (32-bit) |
| LargeAddressAware | **No** | **No** |
| Havok version | **`Havok-5.5.0-r1`** | **`Havok-6.5.0-r1`** |
| D3DX runtime | `d3dx9_38.dll` | `d3dx9_43.dll` |
| Ghidra functions | 25,658 | — |

**Imports (DS1):** `KERNEL32`, `USER32`, `ADVAPI32`, `SHELL32`, `ole32`, `OLEAUT32`, `VERSION`, **`DINPUT8`**, `d3d9`, `d3dx9_38`, `DSOUND`, `WS2_32`, `WINMM`, `XINPUT1_3`, `NETAPI32`

**Imports (DS2):** same set plus `GDI32`, `IMM32`, `IPHLPAPI`; `d3dx9_43` instead of `d3dx9_38`.

> Neither binary is LargeAddressAware, so `LAAPatcher.hpp` applies to **both** games as-is.

> **DRM caveat.** DS2 on Steam ships wrapped in TAGES Solidshield (the `activation.exe`
> prompt). The copy analysed here had already been processed by
> [XTEAther](https://github.com/Wemino/XTEAther), which decrypts the XTEA-protected
> sections, restores functions the DRM had virtualised, removes the DRM sections and
> rewrites the PE entry point — leaving a clean 32 MB executable that never calls into the
> DRM. Consequences:
> - Every "on-disk" DS2 claim in this document (signature matches, Ghidra analysis) is
>   about the **cleaned** binary. Do not expect them to hold for a stock `deadspace2.exe`.
> - The cleaned binary is the *best possible* static-analysis target: fully decrypted and
>   devirtualised. The DS2 Ghidra project was built from it.
> - MarkerPatch's shipped signatures work on stock installs because the game code is
>   identical at runtime; XTEAther changes only the DRM layers around it.
> - Launch DS2 by running `deadspace2.exe` directly. Launching through Steam's app entry
>   runs `activation.exe` first, which sits waiting on an elevated dialog.

---

## 3. Toolchain

Everything below is CLI / scriptable — no GUI tooling is required at any point.

| Tool | Version / path | Purpose |
|---|---|---|
| Ghidra | 12.1.3 PUBLIC — `C:\Tools\ghidra_12.1.3_PUBLIC` | Headless disassembly + decompilation |
| OpenJDK | 21.0.12 — `C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot` | Ghidra prerequisite |
| Python | 3.14 + `pefile` 2024.8.26, `capstone` 5.0.7 | PE parsing, signature scanning, disasm diffing |
| MSVC | VS2022 Community, toolset 14.44 | `dumpbin`, MSBuild |

**Ghidra projects:**

| Project | Path | Analysis time |
|---|---|---|
| `DeadSpace` | `C:\Tools\ghidra_projects` | 160 s |
| `DeadSpace2` | `C:\Tools\ghidra_projects2` (255 MB) | 243 s |

Reproduce an import with:

```
set JAVA_HOME=C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot
C:\Tools\ghidra_12.1.3_PUBLIC\support\analyzeHeadless.bat ^
    C:\Tools\ghidra_projects DeadSpace ^
    -import "D:\GOG\Dead Space\Dead Space.exe" ^
    -processor x86:LE:32:default -max-cpu 8
```

**Not yet installed / optional:** `cdb.exe` (add "Debugging Tools for Windows" to the existing Windows SDK 10.0.26100), Frida, PresentMon, apitrace. None are on the critical path — a diagnostics build of MarkerPatch itself, reusing the scanner and hook helpers in `src/helper.cpp`, covers most runtime instrumentation needs.

---

## 4. Feature port matrix

Legend — **Applies to DS1?**
`YES` verified by evidence in this document · `likely` strong reasoning, not yet verified · `?` unknown, needs investigation · `NO` DS2-only content

Rows marked **Unplanned** under *Port status* are explicitly out of scope for now
(everything from `FixDifficultyRewards` down). They are either DS2-only content or
would need substantial new DS1-specific data; revisit only after the phases in §9.

| Feature | Source | Sigs | DS1? | Port status | Notes |
|---|---|---|---|---|---|
| **HavokPhysicsFix** | `Fixes/HavokPhysicsFix.cpp` | 8 | **YES** | 1 confirmed + 4 candidates | Anchor confirmed; strong BSim candidates for the rest (§5a, §6) |
| **FixSaveStringHandling** | `Fixes/FixSaveStringHandling.cpp` | 2 | **YES** | **✅ verified in-game** | Both sigs 100% identical. A/B on DS1: long-named decoy save crashes vanilla (`0xC000000D`), reaches menu with fix (§8a) |
| **FilterInputDevices** | `Input/FilterInputDevices.cpp` | 2 | **YES** | 1/2 resolved | `IsXInputDevice` 98% identical |
| **LAA patch** | `include/LAAPatcher.hpp` | 0 | **YES** | ready | DS1 lacks the LAA flag; no sigs needed |
| **ASILoader** | `General/ASILoader.cpp` | 0 | **YES** | ready | Game-agnostic |
| **VSyncRefreshRateFix** | `Fixes/VSyncRefreshRateFix.cpp` | 2 | **YES** | **✅ verified by measurement** | DS1 has DS2's exact 30 FPS VSync lock (limiter constant 16.667 ms × 2). DS1-specific port: entry mid-hook on the limiter syncs the constant to the `Window.Hz` setting. Measured: Hz=60 → 60.0 fps, Hz=30 → 30.0 fps on a 60 Hz display (§7a) |
| **RawMouseInput** | `Input/RawMouseInput.cpp` | 12 | **likely** | 0/12 — **hardest** | DS1's FPS-dependent mouse accel is its most-reported bug, so highest user value. But BSim found no good matches (§5a): camera/aiming logic genuinely differs between the games. Needs original RE, not a port |
| **DisableKeyboardHook** | `Input/DisableKeyboardHook.cpp` | 1 | **YES** | **✅ verified in-game** | DS1-specific: both `SetWindowsHookExA` call sites neutered (`+0x29`, `+0xA9`). Windows key / Alt-Tab confirmed working in DS1 (§7a) |
| **UseSDLControllerInput** | `Input/UseSDLControllerInput.cpp` | 0 | **likely** | ready? | DS1 imports `XINPUT1_3`; SDL layer is game-agnostic |
| **FixAutomaticWeaponFireRate** | `Fixes/FixAutomaticWeaponFireRate.cpp` | 2 | **likely** | 0/2 | DS1 has automatic weapons; same cooldown pattern expected |
| **Modding** (dump/mods/stream) | `Modding/*.cpp` | 1 | **likely** | 0/1 | DS1 uses the same SHOC/SHDR streams — see §7 |
| **SkipIntro** | `General/SkipIntro.cpp` | 2 | **YES** | **✅ verified in-game** | DS1-specific: sets the frontend's intro-played flag at state 1 so the state machine jumps straight to the main menu (no logos, no "Press Any Key"). DS2's `LoadMovie` approach does not apply (§7a) |
| **IncreasedEntityPersistence** | `General/IncreasedEntityPersistence.cpp` | 2 | likely | 0/2 | |
| **IncreasedDecalPersistence** | `General/IncreasedDecalPersistence.cpp` | 4 | likely | 0/4 | |
| **TextureFiltering** | `Graphics/TextureFiltering.cpp` | 1 | **YES** | **✅ verified in-game** | DS1-specific implementation (entry mid-hook + 2 verified byte patches). Histogram: 80 % of samplers redirected to aniso ×16; vanilla never used its aniso branch. Visually confirmed (§7a) |
| **AutoResolution** | `Display/AutoResolution.cpp` | 1 | likely | 0/1 | |
| **ResolutionHook** | `Misc/ResolutionHook.cpp` | 2 | likely | 0/2 | |
| **MainLoopHook** | `Misc/MainLoopHook.cpp` | 1 | likely | 0/1 | Prerequisite for physics + input + overlay |
| **D3D9ApiMidHook** | `Misc/D3D9ApiMidHook.cpp` | 4 | likely | 1 candidate | `ResetSite2` 75%; `Present1` is a false positive |
| **HighCoreCPUFix** | `Fixes/HighCoreCPUFix.cpp` | 1 | **?** | 0/1 | Unknown whether DS1 shares the >10-core array overflow |
| **SkipArtificialLoadingDelay** | `General/SkipArtificialLoadingDelay.cpp` | 1 | ? | 0/1 | |
| **DisableOnlineFeatures** | `General/DisableOnlineFeatures.cpp` | 3 | ? | 0/3 | DS1 imports `WS2_32`/`NETAPI32`, but scope differs |
| **FontScaling** | `Display/FontScaling.cpp` | 2 | ? | 0/2 | DS1 subtitle scaling behaviour unverified |
| **DynamicShadowResolution** | `Graphics/DynamicShadowResolution.cpp` | 1 | ? | 0/1 | |
| **FixBlurResolution** | `Fixes/FixBlurResolution.cpp` | 2 | ? | 0/2 | Different D3DX version; renderer may differ |
| **FixShadowBlur** | `Fixes/FixShadowBlur.cpp` | 1 | ? | 0/1 | |
| **FixGlassReflections** | `Fixes/FixGlassReflections.cpp` | 1 | ? | 0/1 | Ships a custom VS (`src/Shaders/`) — may not apply |
| **FixFlareArtifacts** | `Fixes/FixFlareArtifacts.cpp` | 4 | ? | 1 candidate | `FlareTextureSubst` 60% — weak, verify |
| **FixDifficultyRewards** | `Fixes/FixDifficultyRewards.cpp` | 3 | ? | **Unplanned** | DS2 tracks Zealot/Hardcore; DS1's difficulty set differs |
| **AchievementSupport** | `General/AchievementSupport.cpp` | 5 | ? | **Unplanned** | Would need a whole DS1 achievement dataset. If revisited, first verify whether the GOG release already exposes achievements |
| **FixSuitIDConflicts** | `Fixes/FixSuitIDConflicts.cpp` | 1 | **NO** | **Unplanned** | DS2 DLC suit ID collisions |
| **FixSolarArrayElevator** | `Fixes/FixSolarArrayElevator.cpp` | 2 | **NO** | **Unplanned** | DS2 Chapter 7 specific |
| **ShopHooks / DLC** | `DLC/ShopHooks.cpp` | 6 | **NO** | **Unplanned** | DS2 DLC store |

**Totals:** 80 signatures across 30 feature files. 4 confirmed in DS1, 4 likely, 2 false positives, 70 unmatched.

---

## 5. Signature cross-match results

Produced by scanning all 80 repo signatures against both binaries on disk, then verifying each hit by disassembling 40 instructions at both sites and comparing normalised mnemonics (absolute addresses masked).

| Signature | DS2 VA | DS1 VA | Similarity | Verdict |
|---|---|---|---|---|
| `LoadSaveFileList` | `0x01403010` | `0x008EC4D0` | 100% | **confirmed** |
| `CopyStringFromSave` | `0x01401030` | `0x008EA540` | 100% | **confirmed** |
| `IsXInputDevice` | `0x0140DB90` | `0x008F6850` | 98% | **confirmed** |
| `InitializePhysicsSolverParameters` | `0x00475550` | `0x00846500` | 95% | **confirmed** |
| `SetWindowsHook` | `0x0044A270` | `0x00423860` | 78% | likely |
| `ResetSite2` | `0x013CB970` | `0x007C0431` | 75% | likely |
| `FlareTextureSubst` | `0x013D86D9` | `0x007C1ED0` | 60% | likely |
| `LoadedSaveMemoryPtr` | `0x00A45405` | `0x0062B7E3` | 55% | likely |
| `Present1` | `0x013C9F55` | `0x007C0F25` | 50% | **false positive** |
| `BuildContactConstraintJacobian` | `0x004C22D0` | `0x009C0380` | 18% | **false positive** |

> ### Methodology warning for future spelunkers
>
> **Never trust a raw signature cross-match.** Two of ten hits were collisions on a generic MSVC prologue (`55 8B EC 83 E4 F0 …`) that happened to share a stack frame size. Always verify by disassembling both sites and comparing structure.
>
> Also note this scan only finds **byte-identical** code, so it badly undercounts. The 70 "no match" signatures are *not* evidence the functions are absent from DS1 — only that codegen differs. Use Ghidra BSim for structural matching.

---

## 5a. BSim structural cross-match

Byte scanning only finds identical code. BSim compares decompiled structure, so it sees
through the MSVC 2005 vs 2008 codegen gap. Run with
[`tools/ghidra_scripts/BSimCrossMatch.java`](tools/ghidra_scripts/BSimCrossMatch.java)
(`medium_nosize` template — deliberately size-agnostic, since the same logic compiled by
two toolchains differs in size). 79 DS2 signature addresses resolved to 70 functions;
67 were compared against all 25,921 DS1 functions.

### Validation against ground truth

BSim independently reproduced **all four** byte-confirmed matches at 100% similarity.
The one apparent disagreement is instructive rather than a failure:

| Signature | Byte-confirmed DS1 | BSim rank 1 | Note |
|---|---|---|---|
| `InitializePhysicsSolverParameters` | `0x00846500` | `0x00846500` | exact |
| `CopyStringFromSave` | `0x008EA540` | `0x008EA540` | exact |
| `IsXInputDevice` | `0x008F6850` | `0x008F6850` | exact |
| `LoadSaveFileList` | `0x008EC4D0` | `0x008EC420` | **tie** — both score 1.0000 / 48.74 |

`FUN_008ec420` and `FUN_008ec4d0` are adjacent, near-identical sibling functions; rank 3
drops to 0.41. Only 3 of 67 sources showed such a tie. **Where BSim ties, the byte
signature disambiguates** — the two techniques are complementary.

### Reading the scores

**Significance matters more than similarity.** It is BSim's own confidence measure and
scales with function complexity. A 76% similarity at significance 618 on a large function
is far stronger evidence than 100% similarity at significance 29 on a small one, because
small generic functions match everything. Rank candidates by significance first.

### Best candidates (planned features)

| Signature | DS1 candidate | Sim | Signif | Feature |
|---|---|---|---|---|
| `BuildContactConstraintJacobian` | `0x00877320` | 76.5% | **618.7** | HavokPhysicsFix |
| `Build1DAngularConstraintJacobian` | `0x0086AF80` | 89.9% | **131.5** | HavokPhysicsFix |
| `SolveBallSocketChainConstraints` | `0x0086EE40` | 64.1% | **125.6** | HavokPhysicsFix |
| `LoadMovie` | `0x007BD420` | 61.6% | 117.0 | SkipIntro |
| `TX_ChangeOptions` | `0x007D0360` | 100.0% | 77.2 | TextureFiltering |
| `UpdateEngineTimer` | `0x006CF560` | 31.4% | 61.3 | FixAutomaticWeaponFireRate |
| `ProcessEntityDeath` | `0x0047A070` | 46.8% | 58.9 | HavokPhysicsFix |
| `fpsLimiter` | `0x007C0D30` | 59.6% | 58.8 | VSyncRefreshRateFix |
| `CheckFireCooldown` | `0x0050D000` | 78.6% | 51.5 | FixAutomaticWeaponFireRate |
| `SetWindowsHook` | `0x00423860` | 100.0% | 29.2 | DisableKeyboardHook |
| `MainLoop` | `0x004242A0` | 52.8% | 22.9 | MainLoopHook |
| `UpdateDisplaySettings` | `0x007BF9A0` | 100.0% | 21.5 | ResolutionHook |

> **Independent corroboration:** the three Havok candidates (`0x0086AF80`, `0x0086EE40`,
> `0x00877320`) all sit in the same `0x0085`–`0x0087` region as `FUN_0085b980`, the
> Jacobian build phase located separately via Havok profiler strings (§6). Two unrelated
> methods converging on one neighbourhood is strong evidence.
>
> BSim also **rejected** the byte-scan false positive for `BuildContactConstraintJacobian`
> (`0x009C0380`) and proposed a different, far better candidate.

### RawMouseInput is the problem child

All 12 of its signatures scored poorly (22–53% similarity), best being
`UpdatePlayerCamera` → `0x00438070` at 32.8% / 76.7. These are game-logic functions —
camera and aiming systems that genuinely differ between the two games — not shared engine
code. **This feature needs real reverse engineering, not a port.** That is a significant
re-estimate: RawMouseInput has the highest user value for DS1 but is now the largest
unknown, not one of the cheap wins.

### Not compared

Around 20 signatures produced no result: 9 DS2 addresses did not resolve to a function
(mid-function mid-hook sites such as `physicsImpulseDamper`, `constraintErrorScaler`,
`constraintMassCapture`, `CPUFix`, `SetHz`), and 3 fell below the self-significance bound.
Mid-hook sites are byte-offset patches inside larger functions and are not addressable by
function-level matching — they must be re-derived by hand once their containing function
is identified.

---

## 6. Havok physics — deep dive

The highest-value and previously highest-risk feature. Risk has dropped substantially.

### Version gap

DS1 links **Havok-5.5.0-r1**, DS2 links **Havok-6.5.0-r1** — different major versions. An earlier working assumption that they shared a Havok build was **wrong**. What is actually true is narrower: the solver-info struct layout survived the 5.5→6.5 gap unchanged, while other solver internals did not. That precisely explains why the Jacobian builders and ball-socket chain solver failed to cross-match.

### The confirmed anchor

DS1 `FUN_00846500` decompiles to:

```c
void __thiscall FUN_00846500(undefined4 *param_1, int param_2, int param_3)
{
  *param_1     = *(undefined4 *)(param_2 + 0x10c);
  param_1[1]   = *(float *)(param_2 + 0x11c) * *(float *)(param_2 + 0x10c);
  param_1[2]   = *(undefined4 *)(param_2 + 0x110);
  param_1[5]   = *(undefined4 *)(param_2 + 0x120);
  param_1[6]   = *(float *)(param_2 + 0x120) * *(float *)(param_2 + 0x11c);
  param_1[0x10]= *(undefined4 *)(param_2 + 4);
  param_1[0x11]= *(undefined4 *)(param_2 + 8);
  param_1[7]   = *(float *)(param_2 + 0x54) * *(float *)(param_2 + 0x110);
  param_1[8]   = *(undefined4 *)(param_2 + 8);
  param_1[9]   = *(float *)(param_2 + 0x5c) * *(float *)(param_2 + 0x110);
  param_1[3]   = *(undefined4 *)(param_3 + 8);   // <-- delta time
  param_1[4]   = *(undefined4 *)(param_3 + 0xc);
}
```

`param_3 + 8` is the same field DS2's hook reads as `a3[2]` when computing `frameTimeScale = TARGET_FRAME_TIME / a3[2]`. **`InitializePhysicsSolverParameters_Hook` ports essentially unchanged**, adjusting only for the `__thiscall` shape.

### Located DS1 solver functions

Found by resolving Havok's own profiler-marker strings to their containing functions (the markers are emitted *inside* the functions they time):

| DS1 function | Size | Evidence |
|---|---|---|
| `FUN_0085b980` | 2209 | Contains **both** `StBuildJacobians` and `MiNumJacobians` — the Jacobian build phase |
| `FUN_00863750` | 3269 | `MiNumJacobians` |
| `FUN_00862d50` | 405 | `StSolverExport` |
| `FUN_00862ca0` | 170 | `StSolverExport` |

Callers of the anchor `FUN_00846500`: `FUN_00844770` (1127 B), `FUN_00865750` (122 B), `FUN_008b91e0` (1303 B).

### Candidate map for the remaining hooks

Combining the BSim results (§5a) with the profiler-string findings above:

| DS2 hook | DS2 VA | DS1 candidate | Confidence |
|---|---|---|---|
| `InitializePhysicsSolverParameters` | `0x00475550` | `0x00846500` | **confirmed** (byte + BSim, 100%) |
| `BuildContactConstraintJacobian` | `0x004C22D0` | `0x00877320` | strong (signif 618.7) |
| `Build1DAngularConstraintJacobian` | `0x004C55C0` | `0x0086AF80` | strong (signif 131.5) |
| `SolveBallSocketChainConstraints` | `0x004BC2D0` | `0x0086EE40` | good (signif 125.6) |
| `ProcessEntityDeath` | `0x0100FBF0` | `0x0047A070` | moderate (signif 58.9) |
| `physicsImpulseDamper` | `0x004AF9D0` | — | mid-hook, re-derive by hand |
| `constraintErrorScaler` | `0x004AE266` | — | mid-hook, re-derive by hand |
| `constraintMassCapture` | `0x004BFC09` | — | mid-hook, re-derive by hand |

The three mid-hook sites are byte-offset patches *inside* larger functions, so
function-level matching cannot address them. Locate their containing functions first,
then re-derive the offsets against Havok 5.5 — the `hkClass` reflection data above is the
intended route.

### Havok reflection metadata is available

DS1 embeds Havok `hkClass` reflection data, including the member-name strings `buildJacobianFunc`, `solverTau`, `solverDamp`, `solverIterations`, `solverMicrosteps`, `hkpBallSocketConstraintAtom`, `hkpBallSocketChainData`, `correspondingAngLimitSolverResultOffset`.

**This means exact struct member offsets can be parsed out of the binary rather than reverse-engineered by hand** — notably the `cons[7]` field the DS2 hooks divide by `frameTimeScale`. This is the recommended next step for the physics work.

Also present: the `SOLVER_TYPE_*` enum strings (`SOLVER_TYPE_4ITERS_MEDIUM`, etc.), `forceCoherentConstraintOrderingInSolver`, `maxFrictionSolverMassRatio`.

### Two candidate approaches

1. **Mirror DS2.** Re-derive the empirical constants (`0.8f`, `20.0f`, `-30.0f`, `100.0f` in `HavokPhysicsFix.cpp`) against Havok 5.5. Highest fidelity, inherits the tuning burden.
2. **Fix the root cause.** Clamp the physics step to a fixed 1/30 (or 1/60) and run N substeps, decoupling Havok from render rate. Less tuning, more CPU, and risks desyncing framerate-coupled gameplay code.

Recommendation: prototype (2) first for DS1, fall back to (1) if scripted events break. The community's current answer for DS1 is literally "cap at 60", so even a partial fix is a large improvement.

---

## 7. Asset / archive formats

| | DS1 | DS2 |
|---|---|---|
| Container | Loose `.str` per chapter dir | `BIGH` `.DAT` (`DS2DAT0.DAT` … `DS2DAT9.DAT`) |
| Count / size | 787 `.str`, 5.3 GB; 51 `.toc` | 6 `.DAT`, ~8.6 GB |
| Audio | 2958 `.snu` (2.0 GB) | — |
| Video | 486 `.vp6` (2.4 GB) | `.vp6` |
| Inner chunk format | **SHOC / SHDR** | **SHOC / SHDR** |

DS1 `.str` header bytes:

```
33 73 6C 6F  0C 00 00 00  02 00 03 01  43 4F 48 53
                                       ^^^^^^^^^^^ "COHS" = SHOC little-endian
```

The inner stream format matches the FourCCs `ArchiveStreamHook.cpp` already parses (`FOURCC_SHOC`, `FOURCC_SHDR`, `FOURCC_SDAT`, `FOURCC_RPAK`). The **outer** container differs — DS1 has no BIG archive at all, which is if anything simpler. The modding features are therefore promising, pending verification of the stream struct offsets (`OFF_STREAM_RESOURCE 0x108`, `OFF_STREAM_OFFSET 0x10C`, etc.), which are DS2-specific.

---

## 7a. A matching signature does NOT mean a portable feature

Verifying the first batch of "ready" features before wiring them produced a result
worth generalising: **finding the DS1 function is only half the job.** The hook body,
its calling convention, and every struct offset it touches must be verified separately.

| Feature | Function found? | Hook body ports? | Verdict |
|---|---|---|---|
| `FixSaveStringHandling` | yes, 100% identical | yes | **truly drop-in** |
| `LAAPatcher`, `ASILoader` | n/a (no signatures) | yes | **drop-in** |
| `DisableKeyboardHook` | yes, identical instruction-for-instruction | **no — see below** | needs per-game offsets |
| `TextureFiltering` | yes, same function | **no** | needs a rewritten hook |
| `FilterInputDevices` | `IsXInputDevice` only | untested | incomplete |

### `DisableKeyboardHook` — a live footgun

The DS2 patch writes `0xC3` at the function start *and* at `+0x50`. Both games'
installers are structurally identical (`SetWindowsHookEx(WH_KEYBOARD_LL, ...)`), but the
neighbouring code is not:

- **DS2** `+0x50` is the entry point of a separate function (`mov al, [0x2021ee4]`).
- **DS1** `+0x50` is **mid-instruction inside the uninstall routine**
  (`mov dword ptr [0xf420d8], 0`); that routine actually begins at `+0x40`.

Applying the DS2 offset to DS1 would write a `ret` into the middle of a live function.
This is exactly why the per-game **offset** table in §8 is a correctness requirement, not
tidiness.

**Ported on DS1 (2026-09-02)** — `Input/DisableKeyboardHook.cpp`, `ApplyDisableKeyboardHookDS1`.
Both games install the `WH_KEYBOARD_LL` hook from exactly two places (confirmed by IAT
xref: two `call [SetWindowsHookExA]` sites each, same hook procedure). DS1 layout from the
installer at `0x00423860`:

| Offset | What | Patch |
|---|---|---|
| `+0x00` | installer | — |
| `+0x29` | `call [SetWindowsHookExA]` | neutered |
| `+0x40` | uninstaller (`UnhookWindowsHookEx`, guarded by handle != NULL) | — |
| `+0x60` | one-time keyboard-blocking init (DS1's analogue of DS2's `+0x50`) | — |
| `+0xA9` | its `call [SetWindowsHookExA]` | neutered |

Rather than DS2's `ret`-at-function-start, each 6-byte `FF 15 <iat>` call becomes
`31 C0 83 C4 10 90` (`xor eax,eax; add esp,0x10; nop`): the hook handle stays NULL, the
16 bytes of stdcall arguments are balanced, all functions keep their cdecl `ret`, and the
rest of the init logic runs untouched. Each site is verified as `FF 15` before writing.
Log confirmed both sites patched at `0x00423889` / `0x00423909`. DS2 path unchanged and
regression-clean. **Behaviour verified in-game** (Paul, 2026-09-02): Windows key and
Alt-Tab work normally in DS1 with the fix.

### `TextureFiltering` — same function, different shape

DS1's `FUN_007d0360` takes its struct in **`EAX`** (register convention) where DS2 is
`__cdecl(a1, a2)`, and the struct layout differs:

| Field | DS2 offset | DS1 offset |
|---|---|---|
| flags | `+0x10` | `+0x0C` |
| mip filter | `+0x24` | `+0x1C` |
| filter mode (3 = anisotropic) | `+0x25` | `+0x1D` |
| max anisotropy | `+0x27` | `+0x1F` |

DS1 already selects anisotropic natively but hardcodes max anisotropy to **4**, so the
DS1 fix is a smaller change than DS2's — raise that cap and upgrade the other filter
modes. Portable in concept, not in code.

**Ported and verified on DS1 (2026-09-02).** Implementation in
`Graphics/TextureFiltering.cpp` (`ApplyTextureFilteringDS1`):

- `safetyhook::create_mid` at the function entry rewrites flags mode `0` (linear) to
  `0x20000`, so the original takes its own anisotropic branch (min filter 3).
- Two one-byte immediates patched in place, each verified against the expected opcode
  bytes first: `+0x41` (max anisotropy `4` → `MaxAnisotropy`) and `+0x27` (point-branch
  mip filter `1` → `2`, for `ForceTrilinearFiltering`).
- No naked wrapper needed despite the `EAX` convention, because nothing has to run
  *after* the original.

A per-mode histogram in the hook showed, by the main menu (4096 calls):
`linear(0) = 3278, point(1) = 818, anisotropic(2) = 0`. Vanilla DS1 **never** reaches its
anisotropic branch — that path is dead code in practice — and ~80 % of sampler setups
arrive as linear, i.e. exactly the case the hook redirects. `settings.txt` exposes no
filtering option, so the mode is decided per sampler in-engine. Point-mode samplers
(UI / 2D) are left alone, matching the DS2 behaviour.

**Visually confirmed in-game** (Paul, 2026-09-02): a tiled grid floor receding into the
distance was "way sharper" at `MaxAnisotropy = 16` than at `0`. Feature complete for DS1.

In-game histogram after loading a save (32768 calls): `linear 10506, point 22262,
aniso 0`. Point mode dominates during gameplay — consistent with render-target and
post-process samplers (shadow maps, DoF, bloom), which are correctly left untouched.

### `SkipIntro` + `AutoContinue` — the frontend state machine

**Dead end first:** DS1's `LoadMovie` (`0x007BD420`, BSim signif 117, name in `EAX`) was
hooked on the assumption the boot logos were a movie like DS2's `trio_frontend.vp6`. They
are not. The movies it requests at boot (`env_0xx.vp6`, `vid0xx-*.vp6`, chosen at random)
are the menu's *background* loops; suppressing them changed nothing on screen — the EA
logo → Dead Space logo → "Press Any Key" sequence still played. That hook was removed.

**What actually drives the boot:** a frontend state machine. Global state variable
`DAT_00f42210`, `switch` in the frontend update function `FUN_00613050` (signature
`55 8B EC 83 E4 F8 83 EC 3C A1`; the frontend object is `param_1` at `[esp+4]`). States
recovered from the decompile:

| State | Meaning |
|---|---|
| 1 | init. **If `frontend+0x58 != 0` (or no config) → jump to `0x10`.** Otherwise sets `+0x58 = 1`, → 2 or 3 |
| 2 | timed screen (`config+0x27C`), → 3 |
| 3 → 4 → 5 | play logo assets named in the config (`+0x150`, `+0x1B4`) via `FUN_00614610`, wait for each to finish |
| 6 | timed (`+0x280`), → 8 |
| 8 → 9 | **"Press Any Key"**: `FUN_006d4e60` shows it; 9 waits for input (`FUN_006d4ea0`/`FUN_006d4be0`/`FUN_006160e0`) → 10, or on timeout (`+0x284`) plays the attract movie (`+0x218`) → 7 → back to 8 |
| 10 → 0xB → 0xD → 0x1E | fade / transition into the menu |
| **0x10** | builds the main menu (`"DebugLoad"`, `"Continue"` if a save is loadable, `"New_Game"`), → 0x11 |
| **0x11** | main menu active |
| 0x15 | Continue selected: `FUN_006143e0`/`FUN_00614460` resolve the slot and call `vtable+0x28(slot)` (load), → 0x16 |

**Implementation (shipped):** one `create_mid` at the update function's entry; when
state == 1 and `frontend[0x58] == 0`, set it to 1. The game's own shortcut then goes
straight to `0x10` — no logos, no key wait, straight to the main menu. The state
variable's address is taken from the menu's Continue handler
(`80 7C 24 04 00 56 C7 05 ?? ?? ?? ?? 15 00 00 00`, `mov [state], 0x15`, displacement at
`+0x08`), used purely as an anchor. **Verified on screen** (Paul, 2026-09-02).

**Explored and dropped — auto-loading a save.** A `[Debug] AutoContinue` prototype
invoked the Continue handler (`FUN_00615750`, `__stdcall(char)`) at state 0x11, which
opened the load-slot list; the confirm step (state 0x15: save manager at `[0xF62994]`,
`vtable+0x0C` = slot count, `[mgr+4] == 2` = ready, `vtable+0x28(slot)` = load, then
state 0x16) was worked out from `FUN_006143e0` but never shipped: with the intro gone,
loading the last save from the main menu is fast enough, and the patch should land on the
main menu, not in a save. Kept here in case it is ever wanted.

### Developer command-line switches still compiled into DS1

`FUN_00401920` parses (case-insensitive): `-mainloop`, `-gameflow <name>` (copies a 0x40
name into the gameflow object at `DAT_00f6298c+0xC`), `-stream <a> <b>`,
`-playerdontdie` (sets `DAT_00f421bc = 1` — god mode), `-gamemode <x>`, `-checkpoint
<name>` (0x80 string into `DAT_00eb3800`), `-juice`, `-gameid`, `-telemetryname`,
`-newaim`. Also present as strings: `-windowed`, `-fullscreen`, `-vsync`, `-novsync`,
`-resolution`. Whether `-gameflow`/`-checkpoint` boot straight into a level is under test
(`-gameflow ch01_fit` loaded `vid003-spine.vp6` instead of a menu video).

For a direct-to-save boot the alternative is the **Continue** handler `FUN_00615750(0)`:
it sets menu state `0x15` and fires the `"LoadSlot1"` UI action via
`FUN_0061e7c0(frontend, "LoadSlot1", 0)`.

### `VSyncRefreshRateFix` — same limiter, no `SetHz`

**Ported and verified by measurement on DS1 (2026-09-02)** — `Fixes/VSyncRefreshRateFix.cpp`,
`ApplyVSyncRefreshRateFixDS1`.

DS1's frame limiter (`FUN_007c0d30`, BSim candidate, confirmed by decompile) is
structurally identical to DS2's: a `QueryPerformanceCounter` spin-wait against
`param_1 × <per-unit frame-time constant>`, active only when the VSync flag is set. The
"VSync" in both games is this software cap. The constant lives in `.rdata` and defaults to
**16.667 ms with `param_1 = 2`, i.e. a 30 fps cap** regardless of the selected refresh
rate — the DS2 bug, byte for byte.

What differs: DS1 has **no `SetHz` function**. MSVC 2005 inlined it, so the DS2 hook
target does not exist. Two Hz globals were found instead:

| Global | Written by | Value at boot |
|---|---|---|
| `0x00EF6210` | `UpdateDisplaySettings` (`0x007BF9A0`, BSim 100 %) | **0** — only set when settings are applied from the menu |
| `0x00F42640` | settings loader: `GetSettingInt("Window.Hz", 60)` → `mov word ptr [..], ax` | **60** (from `settings.txt`) |

The first attempt used the wrong one, read 0, wrote a 0 constant, and "worked" only because
that disables the limiter and windowed D3D happened to present at 60. Lesson recorded: a
correct-looking frame rate is not proof the cap is following the setting — vary the setting.

Implementation:
- Signatures: limiter `55 8B EC 83 E4 F8 83 EC 2C 53 33 DB 38 1D ?? …` (30 B); settings
  store site `6A 3C B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 04 84 DB 66 A3` (19 B). Hz global
  from the `66 A3` displacement at `+0x13`; frame-time constant from the limiter's
  `fmul dword ptr [..]` displacement at `+0x3E` (DS2's is `+0x37`). Both opcodes verified
  before use.
- `safetyhook::create_mid` at the limiter entry: reads `param_1` from `[esp+4]` and Hz
  from the global, writes `(1000 / Hz) / param_1` to the constant when it differs. Uses
  the real `param_1` rather than DS2's assumed `2`; falls back to the display's current
  refresh rate if Hz reads ≤ 0 rather than disabling the limiter.
- Pacing telemetry in the hook (average ms over 256 limiter calls) so the cap is
  verifiable from the log alone.

| Run (60 Hz display, windowed) | `Window.Hz` | Constant | Measured |
|---|---|---|---|
| 1 | 60 | 16.667 → 8.333 ms | **60.0 fps** |
| 2 | 30 | 16.667 (unchanged) | **30.0 fps** |

The fix only engages when the game's VSync is on (the limiter is otherwise inactive).

---

## 8. Proposed architecture for dual-game support

The current design cannot run against DS1 without changes, for two hard reasons:

1. `Initialization.cpp` triggers `Init()` from a `RegOpenKeyExW` hook watching for
   `SOFTWARE\EA Games\Dead Space 2`. **DS1 can never hit this**, for two independent
   reasons established by static analysis:
   - DS1 imports **`RegOpenKeyExA`** (ANSI) and *not* `RegOpenKeyExW`. The hooked
     function is never called.
   - DS1 has **no game-specific registry key at all**. It makes exactly two
     `RegOpenKeyExA` calls; the only one with a literal key opens
     `SOFTWARE\Microsoft\Direct3D`. The `Electronic Arts\Dead Space` strings in the
     binary are *filesystem* paths (they sit beside `settings.txt` and My Documents
     context), not registry paths.

   **Recommended trigger:** both games import `Direct3DCreate9` from `d3d9.dll`.
   That is called exactly once at startup, after the module is fully loaded and long
   before gameplay, and works identically for both titles. Leave DS2's proven
   registry trigger alone to avoid regressions, and use the D3D trigger for DS1.
2. `ScanModuleSignature` raises a `MessageBoxA` on every miss. A naive dual-game build would show ~80 error dialogs on DS1.

### Phase 1 refactor (no reverse engineering required)

1. **Game detection** — replace the registry trigger with exe name + `SizeOfImage` / timestamp + anchor signatures, resolving to `enum class Game { DS1, DS2 }`. Keep a deferred trigger: the proxy loads before the game's entry point.
2. **Signature registry** — move all 80 patterns out of feature code into a per-game table keyed by symbolic name. Features call `Sig("InitializePhysicsSolverParameters")` and get the right pattern for the running game.
3. **Offset registry** — the same for hardcoded struct offsets and magic constants (`thisp + 800` fire delay, `esp + 0x158`, `eax + 0x0C` tau, ...).
4. **Feature capability flags** — each `Apply*` declares supported games; `Init()` iterates a registry and silently skips unsupported features instead of unconditionally calling all 30.
5. **Config** — one `MarkerPatch.ini`, per-game sections written on first run so DS1 users never see `EnableSeveredDLC`.

Doing this **first** means every subsequent DS1 finding is a one-line table entry rather than a code change.

## 8a. Boot slice — what is implemented (branch `ds1-port`)

A deliberately thin vertical slice, built to retire the "does it even initialise on DS1"
risk before any broader refactor. It changed 7 files, +194 / −3 lines.

| Piece | Where | Notes |
|---|---|---|
| `enum class Game` + `g_State.game` | `Globals.cpp` | `Unknown` / `DS1` / `DS2` |
| `DetectGame()` | `Globals.cpp` | Executable name first (`deadspace2.exe`, `dead space.exe`, `deadspace.exe`); falls back to scanning for the embedded `Havok-5.5.0` / `Havok-6.5.0` version string, so a renamed exe still detects |
| `struct GameSig` + `ScanModuleSignature(GameSig)` overload | `Globals.cpp` | One pattern per game; `nullptr` = unsupported, returns 0 with no error dialog. Existing string-literal call sites are untouched |
| DS1 init trigger | `Initialization.cpp`, `dllmain.cpp` | Inline hook on `d3d9!Direct3DCreate9`. DS2 keeps its registry trigger unchanged; an unrecognised exe installs the registry hook and simply never fires |
| `InitDeadSpace1()` | `Initialization.cpp` | Explicit allow-list: `FixSaveStringHandling`, `ASILoader` (+ LAA if configured). Nothing else runs on DS1 |
| `LogHelper` | `helper.cpp` | `[Debug] LogFile = 1` writes `MarkerPatch.log` beside the DLL: detection, trigger, every scan result |

Build note: the project targets toolset `v145`; on VS2022 pass `/p:PlatformToolset=v143`
on the MSBuild command line rather than editing the upstream project file.

### First DS1 boot (GOG build)

```
MarkerPatch loaded into Dead Space (UAL=0)
Init: Dead Space  module=0x00400000
  LoadSaveFileList                     0x008EC4D0
  CopyStringFromSave                   0x008EA540
Init complete
```

Trigger fired ~1 s after launch. Both addresses match the static analysis in §5 exactly.
No error dialogs. This validates the whole chain: proxy load → detection → D3D trigger →
pattern scan → hook install, on a binary the code had never run in.

### `FixSaveStringHandling` verified in-game on DS1 (A/B, 2026-09-02)

DS1 enumerates saves **at startup**, twice (boot and main menu), with no menu
navigation needed. Hook logging (gated on `[Debug] LogFile`) showed the exact pattern:

```
LoadSaveFileList: pattern "ds_slot_*.deadspacesaved" -> "ds_slot_??.deadspacesaved" (rewritten), result=0
CopyStringFromSave: "Dead Space: Slot 1" (18 chars)
CopyStringFromSave: "Chapter 2: Intensive Care - Medium" (34 chars)
CopyStringFromSave: "Time: (1:02:50) - Rd: 1" (23 chars)
CopyStringFromSave: "Slot 1 (Time 1:02:50 Diff 1 Ch 2 Checkpoint 1 Rd 1)" (51 chars)
```

DS1 passes exactly the string the hook rewrites, so the fix applies. `CopyStringFromSave`
turns out to copy the slot's *metadata strings* (title, chapter, time, summary), not only
filenames — so the 128-wchar buffer is shared by several callers.

Then a decoy save was planted in `Documents\Electronic Arts\Dead Space\` — a byte-copy of
the real save renamed to a **165-character** `ds_slot_01XXX….deadspacesaved`, so name
length was the only variable:

| Run | `FixSaveStringHandling` | Decoy | Outcome |
|---|---|---|---|
| A | on | present | Enumeration saw only Slot 1; **main menu reached** |
| B | **off** | present | **Process died at startup, exit code `0xC000000D`** (`STATUS_INVALID_PARAMETER` — CRT fast-fail on the overflowing copy) |

So DS1 has the bug, and the ported fix corrects it. The decoy was removed and the save
folder hash-verified against the pre-test backup
(`Documents\DeadSpace1_saves_backup_2026-09-02_0003`, SHA-256 on every file).

### DS2 regression run (Ultimate ASI Loader path)

Same build deployed as `plugins\MarkerPatch.asi` under UAL 9.7.1, launched by running
`deadspace2.exe` directly:

```
MarkerPatch loaded into Dead Space 2 (UAL=1)
Init: Dead Space 2  module=0x00400000
  ... all signatures resolved, no NOT FOUND ...
Init complete
```

So the `DllMain` changes (detection, logging, trigger selection) are regression-free on
DS2, and the UAL code path (`isUALPresent`, proxy setup skipped) works with the new
detection.

> **Hazard — never keep a backup `.asi` inside `plugins\` or `scripts\`.** Confirmed from
> UAL's source (`dllmain.cpp`): it scans root `*.asi`, then `scripts\` and `plugins\`
> **recursively by default** (`[globalsets] LoadRecursively`, default `TRUE`). A backup in
> `plugins\bak\` therefore loads as a second instance. Two MarkerPatch instances in one
> process is a guaranteed cascade of "Unable to find signature" dialogs: the first
> instance's inline hooks overwrite each target's prologue with a `JMP`, so the second's
> scans fail for exactly the functions being patched. Park backups in a root-level folder
> that is not `scripts\` or `plugins\` (root itself is scanned non-recursively).
>
> Two more UAL facts that matter for testing: it skips a plugin whose full path is already
> loaded (`GetModuleHandle` check), and it **silently ignores `ERROR_DLL_INIT_FAILED`** —
> a plugin whose `DllMain` fails simply does not load, with no dialog and no log. UAL
> writes no log file of its own.

### Lesson from a false alarm

Several DS2 runs produced *no* `MarkerPatch.log`, which was read as "our DLL is not
loading". A breadcrumb build (raw-Win32 writes from `DllMain`, wrapped in SEH) proved
`DllMain` ran to completion every time — the deployed ini simply had `LogFile = 0`
because a deploy script's text replacement had silently not applied.

Rules that fall out of this:

- **Absence of the log is not evidence of absence of the DLL.** Verify the deployed ini's
  `LogFile` value by reading it back before drawing any conclusion.
- 64-bit tooling (`Get-Process .Modules`, `tasklist /m`) cannot enumerate the modules of
  this 32-bit process from an unelevated session — it sees only the WoW64 shim. Don't
  use it to decide whether a plugin loaded.
- If you do need to know where `DllMain` dies under UAL, the breadcrumb approach works:
  `GetModuleFileNameW(hModule)` for the path, `CreateFileW`/`WriteFile` for the write,
  `__try` around a helper (not around code with C++ destructors, which is C2712).

---

## 9. Roadmap

| Phase | Work | RE required | Status |
|---|---|---|---|
| 0 | Recon: imports, DRM, Havok version, binary facts, cross-match | minimal | **done** |
| 1 | Multi-game refactor (registry, gating, detection) | **none** | not started |
| 2 | BSim structural diff to resolve the 70 unmatched signatures | moderate | **done** (§5a) |
| 3 | Cheap wins: LAA, texture filtering, VSync fix, keyboard hook, skip intro, resolution | moderate | **5 of 6 done** — LAA ready, texture filtering ✅, VSync ✅, keyboard hook ✅, skip intro ✅; resolution not started |
| 4 | Physics | heavy | 1 confirmed + 4 candidates |
| 4b | RawMouseInput — original RE, no usable DS2 correspondence | heavy | not started |
| 5 | Modding / archive support (SHOC-SHDR streams) | heavy | not started |

Achievements, DLC/shop, and the DS2-only save and level fixes are **out of scope** —
see the Unplanned rows in §4.

Phase 3 alone would already exceed the best currently available DS1 patch, since it replaces the two mods the community presently recommends (Dead Space Mouse Fix and the VSync lag fix).

---

## 9a. Future: in-game debug overlay (Paul's suggestion, 2026-09-02)

A Dear ImGui panel to enable/disable individual hooks at runtime and show what is
currently active. Most plumbing already exists: `include/imgui/`, `imgui_impl_dx9`,
and the `D3D9ApiMidHook` `Present`/`Reset` sites used by `AchievementOverlay`. Needs:

- DS1 `Present`/`Reset` hook sites (`D3D9ApiMidHook` signatures are unresolved on DS1;
  `ResetSite2` is a 75 % candidate at `0x007C0431`, `Present1` was a false positive).
- A feature registry with runtime enable/disable — which is the Phase 1 refactor's
  capability-flag table, so the two pieces of work converge.
- Per-hook "installed / active / last fired" state, which `LogHelper` already tracks
  informally.

Deferred until the port itself is further along.

## 10. Open questions

- ~~What registry key / init trigger does DS1 use?~~ **Answered — see §8.**
- **Does the GOG DS1 release already expose achievements?** If yes, `AchievementSupport` is DS2-only and a large chunk of Phase 5 disappears.
- **Does DS1 share the >10-core CPU array overflow?** Determines `HighCoreCPUFix`.
- **Do the DS1 renderer fixes apply at all?** Different D3DX version; the blur/shadow/flare/glass fixes may target DS2-only rendering paths.
- **Upstream intent** — this repo is a fork of `Wemino/MarkerPatch`. A dual-game refactor is a large structural change; worth agreeing with upstream before Phase 1.

---

## 11. Verification scripts

Every table in this document is reproducible from [`tools/`](tools/). See
[`tools/README.md`](tools/README.md) for setup and the typical workflow.

| Script | Purpose |
|---|---|
| `tools/dsbin.py` | Shared library: signature parsing, PE offset/VA translation, disassembly normalisation, similarity scoring |
| `tools/sigscan.py` | Scan all repo signatures against both binaries on disk |
| `tools/classify.py` | Score cross-matches and classify confirmed / likely / false positive (`--disasm` for side-by-side listings) |
| `tools/xref.py` | Resolve a string to its containing function by immediate-value xref |
| `tools/table.py` | Generate the per-feature port matrix (emits `sigtable.json`) |
| `tools/ghidra_scripts/ExportNeighborhood.java` | Ghidra post-script: function inventory CSV + decompile call-graph neighbourhood around anchor addresses |
| `tools/ghidra_scripts/BSimCrossMatch.java` | Headless BSim cross-binary function matching (§5a) |

Requires `pip install pefile capstone`. Game paths default to a typical install
and can be overridden with `MARKERPATCH_DS1` / `MARKERPATCH_DS2`.
