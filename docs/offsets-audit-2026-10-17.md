# Audit of the hand-written offset headers against the generated tables

Scope: every address-valued constant under `client/warcraftxl/include/wxl/offsets/` (32 headers, `engine/` and `game/`), joined against `experiments/WarcraftXL/core/offsets/functions.def` (26,077 rows) and `statics.def` (5,240 rows). Each hit was judged against the constant's own comment, and every doubtful hit was read in the decompiled source under `experiments/WarcraftXL/client/`. Mid-function and data addresses were located with a linear disassembly of `client/game/base/Wow.exe` from the nearest reconstructed function start. Field offsets (small `size_t` values) are inventoried at the end, not audited.

Rows: one row per address literal. Arrays and struct initialisers contribute one row per element (1118 rows, 1113 distinct constants). `game/M2.hpp:362 kEmitterIgnoreDistance = 0x400000` is a flag value, not an address, and is excluded.

## Summary

| bucket | rows |
|---|---|
| CONFIRMED | 978 |
| MISMATCH | 4 |
| FOLDED | 4 |
| UNCERTAIN | 3 |
| ABSENT | 129 |
| total | 1118 |

Per header (CONFIRMED = the generated symbol is a plausible match for the comment; naming style differs by design):

| header | CONFIRMED | MISMATCH | FOLDED | UNCERTAIN | ABSENT |
|---|---|---|---|---|---|
| `engine/Addon.hpp` | 16 | 0 | 0 | 0 | 10 |
| `engine/Boot.hpp` | 1 | 0 | 0 | 0 | 0 |
| `engine/Camera.hpp` | 10 | 1 | 0 | 0 | 1 |
| `engine/Draw.hpp` | 3 | 0 | 0 | 0 | 0 |
| `engine/Frame.hpp` | 3 | 0 | 0 | 0 | 0 |
| `engine/Gx.hpp` | 27 | 0 | 1 | 0 | 12 |
| `engine/GxDevice.hpp` | 33 | 0 | 2 | 0 | 4 |
| `engine/Io.hpp` | 14 | 0 | 0 | 1 | 2 |
| `engine/Layout.hpp` | 3 | 0 | 0 | 0 | 0 |
| `engine/Liquid.hpp` | 18 | 0 | 0 | 0 | 3 |
| `engine/Lua.hpp` | 56 | 0 | 0 | 0 | 0 |
| `engine/Mem.hpp` | 5 | 0 | 0 | 0 | 0 |
| `engine/Shader.hpp` | 32 | 0 | 1 | 0 | 7 |
| `engine/Sky.hpp` | 13 | 0 | 0 | 0 | 2 |
| `engine/Sound.hpp` | 4 | 0 | 0 | 0 | 1 |
| `engine/Ui.hpp` | 1 | 0 | 0 | 0 | 0 |
| `engine/Xml.hpp` | 28 | 0 | 0 | 0 | 0 |
| `engine/Zlib.hpp` | 0 | 0 | 0 | 0 | 3 |
| `game/ADT.hpp` | 131 | 0 | 0 | 0 | 6 |
| `game/CharCreate.hpp` | 2 | 0 | 0 | 0 | 2 |
| `game/DB2.hpp` | 11 | 0 | 0 | 0 | 24 |
| `game/Dbc.hpp` | 13 | 0 | 0 | 0 | 8 |
| `game/Doodad.hpp` | 5 | 0 | 0 | 0 | 0 |
| `game/GameObject.hpp` | 0 | 0 | 0 | 0 | 3 |
| `game/GroundEffect.hpp` | 20 | 0 | 0 | 0 | 5 |
| `game/M2.hpp` | 255 | 0 | 0 | 0 | 29 |
| `game/Unit.hpp` | 8 | 1 | 0 | 0 | 0 |
| `game/WMO.hpp` | 161 | 1 | 0 | 0 | 2 |
| `game/Weather.hpp` | 5 | 0 | 0 | 0 | 0 |
| `game/World.hpp` | 87 | 1 | 0 | 2 | 5 |
| `game/WorldMap.hpp` | 4 | 0 | 0 | 0 | 0 |
| `game/WorldScene.hpp` | 9 | 0 | 0 | 0 | 0 |

## MISMATCH (4)

Most important first. "Live" means the core or an extension reads the constant today; "dormant" means nothing does, so the error is latent.

### 1. `camera::kViewProj = 0x00ADF460` is the occlusion clip-buffer matrix, not View * Projection (live)

- Constant: `client/warcraftxl/include/wxl/offsets/engine/Camera.hpp:29`, in the block documented as "the world camera matrices, as static globals ... float[16], row-major". No per-constant comment beyond the name.
- Generated: `CWorldScene::s_clipBufferMatrix` (`statics.def`, `WoW/Source/WorldClient/World.cpp:351`).
- What it is (`experiments/WarcraftXL/client/WoW/Source/WorldClient/World.cpp:2876-2883`, inside `CWorldScene::Update` 0x00795400 [auto]):

  ```cpp
  if (lenSqXY > 0.000099999997f) {
      C44Matrix lookAt;
      GxuXformCreateLookAtSgCompat(C3Vector(0.0f, 0.0f, 0.0f), dirXY, C3Vector(0.0f, 0.0f, 1.0f), lookAt);
      lookAt.Translate(-CWorldScene::s_activeWorldView);
      CWorldScene::s_clipBufferMatrix = lookAt * CWorldScene::projMatrix;
  } else {
      CWorldScene::s_clipBufferMatrix = C44Matrix();
  }
  ```

  A view-projection built from the camera direction flattened onto the XY plane (`dirXY`), i.e. with pitch removed, and reset to identity when the camera looks straight down. Its only readers are the horizon occlusion code (`CWorldOcclusion.cpp:342, 424, 490, 541`).
- The matrix the comment means: `CWorldScene::s_viewProjMatrix = 0x00ADF5A8` (`World.cpp:378`), computed at `World.cpp:2838` as `CWorldScene::viewMatrix * CWorldScene::projMatrix`, right after `viewMatrix.Translate(-s_activeWorldView)`. Its neighbours are the two matrices the header already has right: `kView = 0x00ADF5E8 = CWorldScene::viewMatrix`, `kProjection = 0x00ADF628 = CWorldScene::projMatrix`.
- Impact: `client/warcraftxl/include/wxl/game/Camera.hpp:47` (`GetViewProj()`) returns the occlusion matrix, and `:107` (`SetViewProj()`, documented "must equal View * Projection or culling disagrees with the draw") writes it, which only moves the horizon-occlusion test and leaves the real view-projection untouched.
- Confidence: high on the identity of both addresses (statics are declared with their addresses, the construction is read in the source); high that the intent is View * Projection, from the name and the block comment.

### 2. `world::kFileExists = 0x00422170` is `SFileSetFileBlockEntryMD5`, not a file-exists test (dormant)

- Constant: `client/warcraftxl/include/wxl/offsets/game/World.hpp:86`. Comment: "Archive file-exists test. __stdcall taking two args (ret 8): (value, &pathObject), not a bare C string. Resolves against the Client's own archive/loose set only ..."
- Generated: `SFileSetFileBlockEntryMD5` [auto], `Engine/Source/BlizzardCore/SFile2-Core.cpp:349`:

  ```cpp
  int32_t STORMAPI SFileSetFileBlockEntryMD5(const char* name, const MD5& md5) {
      Blizzard::Util::Digest<16> digest;
      md5.ToArray(reinterpret_cast<uint8_t*>(&digest));
      return Blizzard::Mopaq::SetFileSignature(name, digest) != 0;
  }
  ```

  Two `__stdcall` arguments, so the shape ("ret 8, second arg by reference") is what the hand entry observed, but the body stores a block's MD5 into the archive's signature table. It returns non-zero on success, so it probably even "answers" like an exists test for a file that is in an archive, which is how the wrong address could have survived.
- What matches the intent: `SFile::FileExistsEx` 0x00424B10 [auto] (`SFile2-Core.cpp:1020`, `STORMAPI (const char* name, uint32_t flags)`, body `System_SFile2::FindFile(name, path, 260, flags, &type, nullptr)`) is the two-argument `ret 8` entry; `SFile::FileExists` 0x00424E50 [auto] (`:1135`, one argument) is the plain one; `SFile::FileExistsInArchives` 0x00421FC0 [auto] (`:621`) is the archive-only variant. `client/warcraftxl/extensions/wxl-host-client/src/files/Engine.hpp:38-39` already carries `kFileExists = 0x00424B10` and `kFileExistsAny = 0x00424E50` under its own names, so the extension was corrected locally without the shared header following.
- Impact: nothing under `client/warcraftxl/src` or `extensions/` references `world::kFileExists` (grep), so the error is dormant.
- Confidence: high.

### 3. `wmo::kSetupInstanceMatrices = 0x007F3E00` is a minimap helper, not the WMO draw's matrix publish (dormant)

- Constant: `client/warcraftxl/include/wxl/offsets/game/WMO.hpp:655`. Comment: "Publishes the WMO world/view matrices used by the map-object draw, the seam for injecting a per-instance scale or a shadow-pass matrix override. __cdecl, caller-cleaned."
- Generated: `SetupMapObjMatrices` [auto], a file-static in `WoW/Source/Ui/MinimapFrame.cpp:1358`:

  ```cpp
  static void SetupMapObjMatrices(CMapEntity* entity) {
      World::QueryMapObjMatrix(entity, &s_mapObjMatrix, &s_mapObjInvMatrix);
      ...  // renormalise the three basis rows of s_mapObjMatrix
      s_mapObjMatrix.d0 = 0.0f; s_mapObjMatrix.d1 = 0.0f; s_mapObjMatrix.d2 = 0.0f;
      s_mapObjMatrix.RotateAroundZ(s_mapObjRotation * 0.017453292f);
  }
  ```

  It asks the world for the matrix of the WMO the given entity stands in and bends it into the minimap frame's own `s_mapObjMatrix` (translation zeroed, rotated by the minimap rotation). Nothing in the render path reads those statics.
- What matches the intent: there is no separate "publish" function. The per-instance matrices are built once by `CMapObjDef::UpdateMatrix` 0x007B67B0 [manual] (`WoW/Source/WorldClient/MapObjDef.cpp:149`: writes `m_position`, `m_mat`, `m_invMat = matrix.AffineInverse()`, then `UpdateMoved`), which the header already has as `kInstanceUpdateMatrix` at `WMO.hpp:761` with the right comment ("exactly where a per-instance scale can be injected into both matrices at once"). The draw reads `def->m_mat` / `m_invMat` directly (`CMapObjDef::RenderWithCallback` 0x007B3B20 passes them to `RenderInterior`; `CWorldScene::RenderMapObjDefGroups` 0x007964A0 = `kSceneRenderInstanceGroups`). A shadow-pass override belongs on `CMapObj::RenderMapObjGroupsShadowMap` 0x007AB760 (`kRenderShadowMapGroups`).
- Impact: no user in the core or the extensions (grep); dormant.
- Confidence: high.

### 4. `unit::kTargetSet = 0x0051A5C0` is `Script_CancelPendingEquip` (the one already found)

- Constant: `client/warcraftxl/include/wxl/offsets/game/Unit.hpp:56`. Comment: "Target-set script function: resolves a unit token and sets the player's current target (writes kTargetGuid). __cdecl(scriptState)".
- Generated: `Script_CancelPendingEquip` [auto], `WoW/Source/Ui/GameUI_Scripts.cpp:4520`: `lua_isnumber(L, 1)` guard, `ClntObjMgrGetActivePlayerObj()->ClearPendingEquip(index, 0)`.
- What matches the intent: `Script_TargetUnit` 0x00525A30 [auto], `GameUI_Scripts.cpp:5896`: `ClosestObjectMatch(name, TYPEMASK_UNIT, ...)` then `TargetIfValid(guid)`. `kTargetGuid = 0x00BD07B0` is `CGGameUI::m_lockedTarget` (`GameUI.cpp:1034`), the field the selection circle and spell targeting read, so the POST-call read the comment prescribes is sound once the address is fixed.
- Confidence: high.

## FOLDED (4)

Addresses the generated table lists under several different names (identical bodies merged by the linker). A hook there fires for every one of them.

| constant | address | names | examples | verdict |
|---|---|---|---|---|
| `gx::kTexFillParked` (`engine/Gx.hpp:303`) | `0x005EEB70` | 93 | `BSN::Encoder::Flush`, `CCharacterSelection::Initialize`, `CGActionBar::LeaveWorld`, `CGGameObject_C_TypeBase::Disable`, `Battlenet::Tendril::DoSubscriptions`, ... | Expected and documented: the comment says "nullsub: a CGxTex parked in CGxTexCache, userArg 0". It is the binary's shared empty function. Never hook it; compare against it only. By far the worst fold in the table. |
| `gxdevice::kDeviceOverride` (`engine/GxDevice.hpp:527`) | `0x0069FF40` | 2 | `CGxDeviceD3d::DeviceOverride` [manual], `CGxDeviceD3d9Ex::DeviceOverride` [manual] | Documented: "Vtable slot 28, shared by the D3D9 and D3D9Ex tables". |
| `gxdevice::kWindowProc` (`engine/GxDevice.hpp:712`) | `0x006A0360` | 2 | `CGxDeviceD3d::WindowProcD3d` [manual], `CGxDeviceD3d9Ex::WindowProcD3d` [manual] | Documented: "The window procedure both D3D window classes register". |
| `shader::kShaderCreateVertex` (`engine/Shader.hpp:78`) | `0x006AA0D0` | 2 | `CGxDeviceD3d::IShaderCreateVertex` [auto], `CGxDeviceD3d9Ex::IShaderCreateVertex` [auto] | Both backends share the body; the comment already treats it as "the device's vertex-shader create entry", which holds for either. |

Not folds, but worth knowing for a by-name migration: 689 generated addresses carry one name tagged from two files (the `Win32/` and `Win64/` twins, e.g. `OsValidateFunctionPointer` 0x0086B5A0 appears as [auto] from `System.cpp:403` and [none] from `System.cpp:354`, which is what `lua::kValidateFunctionPointer` hits). The hand-written table only meets that case there.

## UNCERTAIN (3)

### `world::kEnter = 0x00781500` (`game/World.hpp:35`): the address is `World::Preload`, the comment overstates what it does (live)

- Comment: "CWorld::Enter(time, withLoadingScreen): unloads the old world and loads the new one (calls the blocking load), then dismisses the loading screen. At entry the old world is still intact (leave point); after it returns the new world + objects are resident (enter point)."
- Generated: `World::Preload(const C3Vector& position, int32_t showLoading)` [manual], `WoW/Source/WorldClient/World.cpp:1014`: `PrepareAreaOfInterest(position)`; if `showLoading`, install the loading-screen progress callbacks and `LoadingScreenEnable(s_mapId, 1)`; `CMap::Preload()` (the blocking drain); if `showLoading`, `LoadingScreenDisable()`.
- What agrees: it is the blocking load with the loading screen around it, and it is what the new-world path calls (`MovementUpdateWorld`, `Movement_C.cpp:244`: `World::Preload(position, 1)` when `preload` is set, else `World::Update(position)`). Other callers (`Player_C.cpp:3134`, `GameUI.cpp:5549/6833`, `GameObject_C.cpp:3552`) use it for far teleports without a map change.
- What does not: it unloads nothing and loads no map. The map change is `CWorld::LoadMap` 0x00781430 (`kMapLoadBegin`, calls `CMap::Load` = `kMapEnter` which writes `kCurrentMapId`) and `World::UnloadMap` 0x00783180 (`kMapUnload`), both already in the header. Whether `kCurrentMapId` still holds the old id at `Preload` entry therefore depends on the caller's order, not on this function; the core's `hkWorldEnter` (`client/warcraftxl/src/client/CWorldScene/World.cpp:47`) relies on that.
- Also fires on teleports inside one map, so an OnWorldLeave/OnWorldEnter pair hung on it reports a "world change" with the same map id on both sides.
- Why not MISMATCH: the function is in the right family and is the one the loading-screen transition passes through; the comment's wording and the typedef (see Signature disagreements) are what is off. Suggested reading before deciding: `World.cpp:1014-1037` and `Movement_C.cpp:236-250`.

### `io::kFileOpen2 = 0x00422040` (`engine/Io.hpp:27`): no documented intent

- The constant has no comment; the name says "a second file open". Generated: `SFileOpenArchive(const char* path, int32_t priority, uint32_t flags, SArchive** archive)` STORMAPI [manual], `SFile2-Core.cpp:645`, the public wrapper over `Blizzard::Mopaq::SFileOpenArchive` (`kMopaqOpenArchive` 0x0045C480). It opens an archive, not a file. Nothing in the core or the extensions uses it (grep). Either rename it (`kArchiveOpen`?) or drop it; with no statement of intent there is nothing to audit against.

### `world::kActiveListLinkBase = 0x00ADFBEC` / `kActiveListHead = 0x00ADFBF4` (`game/World.hpp:67-68`): the decompilation does not name the list

- Generated: `CMap::s_unk_ADFBEC`, a `TSList<CMapBaseObjLink, TSGetExplicitLink<CMapBaseObjLink>>` (`WoW/Source/WorldClient/Map.cpp:127`); `kActiveListHead` is +8 inside it (ABSENT row). The hand comment's node shape (tile = `*(node+4)`, next through the link base) fits a `CMapBaseObjLink { link, m_owner }` list, so nothing contradicts "active-tiles list", but the decompilation could not say what the list holds either. Low concern; flagged only because this is the one place where neither side can vouch for the other.

## ABSENT (in neither table)

None of these is a wrong address in the MISMATCH sense: every one resolves to a place inside a known function, inside a known static, to an `.rdata` item the generator does not tag (strings, vtables, float literals), or to one of seven code addresses the decompilation has not reconstructed (five functions, a thunk and the CRT). `inside F +0xNN` means the address lies between F's start and the first `int3` padding after it (linear sweep of Wow.exe); `operand` means it is not an instruction boundary but an immediate or displacement inside an instruction, which is what the comment says it is.


### `client/warcraftxl/include/wxl/offsets/engine/Addon.hpp`

| line | constant | address | where |
|---|---|---|---|
| 131 | `kSignatureStatusJumpTable` | `0x0052aeb4` | inside `CGGameUI::InitializeFrameXML` +0x534: the jump-table data that follows the function's code, as the comment says. |
| 132 | `kMsgFrameXmlNoSignature` | `0x00a02fb0` | `.rdata` item (string / vtable / literal), untagged; between `s_unitSexValues` 0x9fe7ec and `s_formatBits` 0xa0c7c0 |
| 133 | `kMsgFrameXmlBadSignature` | `0x00a02f90` | `.rdata` item (string / vtable / literal), untagged; between `s_unitSexValues` 0x9fe7ec and `s_formatBits` 0xa0c7c0 |
| 134 | `kMsgFrameXmlModified` | `0x00a02f70` | `.rdata` item (string / vtable / literal), untagged; between `s_unitSexValues` 0x9fe7ec and `s_formatBits` 0xa0c7c0 |
| 143 | `kFrameXmlTocPath` | `0x00a02fcc` | `.rdata` item (string / vtable / literal), untagged; between `s_unitSexValues` 0x9fe7ec and `s_formatBits` 0xa0c7c0 |
| 144 | `kFrameXmlBindingsPath` | `0x00a02fec` | `.rdata` item (string / vtable / literal), untagged; between `s_unitSexValues` 0x9fe7ec and `s_formatBits` 0xa0c7c0 |
| 151 | `kTocSecureKey` | `0x00a1d970` | `.rdata` item (string / vtable / literal), untagged; between `s_emptyMd5` 0xa1d648 and `s_smoothViewDataMin` 0xa1e368 |
| 155 | `kSecurityCorrupt` | `0x00a1d6e8` | `.rdata` item (string / vtable / literal), untagged; between `s_emptyMd5` 0xa1d648 and `s_smoothViewDataMin` 0xa1e368 |
| 156 | `kSecurityInsecure` | `0x00a1d6f0` | `.rdata` item (string / vtable / literal), untagged; between `s_emptyMd5` 0xa1d648 and `s_smoothViewDataMin` 0xa1e368 |
| 157 | `kSecuritySecure` | `0x00a1d730` | `.rdata` item (string / vtable / literal), untagged; between `s_emptyMd5` 0xa1d648 and `s_smoothViewDataMin` 0xa1e368 |

### `client/warcraftxl/include/wxl/offsets/engine/Camera.hpp`

| line | constant | address | where |
|---|---|---|---|
| 45 | `kSimpleCameraVTable` | `0x00a1e864` | `.rdata` item (string / vtable / literal), untagged; between `s_smoothViewDataMax` 0xa1e370 and `s_terrainTiltTable` 0xa1e9c8 |

### `client/warcraftxl/include/wxl/offsets/engine/Gx.hpp`

| line | constant | address | where |
|---|---|---|---|
| 43 | `kBlendSrcReadDisp` | `0x006a4d94` | inside `CGxDeviceD3d::IRsSendToHw` +0x164 (operand) |
| 44 | `kBlendDstReadDisp` | `0x006a4dbd` | inside `CGxDeviceD3d::IRsSendToHw` +0x18d (operand) |
| 101 | `kGxDeviceVTable` | `0x00a2e718` | `.rdata` item (string / vtable / literal), untagged; between `s_d3dQueryTypes` 0xa2e4e4 and `s_glMinFilter` 0xa2f1e0 |
| 265 | `kWorldRenderEpilogueAnchor` | `0x004fb074` | inside `CGWorldFrame::RenderWorld` +0xe4 (instruction) |
| 297 | `kTexHandleVTable` | `0x009f13b4` | `.rdata` item (string / vtable / literal), untagged; between `s_texFormatTexelsPerBlock` 0x9f1154 and `s_rndTable` 0x9f1700 |
| 325 | `kMipScratchDimHImm` | `0x004b7f8d` | inside `TextureInitialize` +0xd (operand) |
| 326 | `kMipScratchDimWImm` | `0x004b7f92` | inside `TextureInitialize` +0x12 (operand) |
| 409 | `kVsConstCache` | `0x00c5efe8` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x1008 (`.data`) |
| 410 | `kVsDirtyRegStart` | `0x00c5ffec` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x200c (`.data`) |
| 411 | `kVsDirtyRegEnd` | `0x00c5ffe8` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x2008 (`.data`) |
| 427 | `kPsDirtyRegStart` | `0x00c5efe4` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x1004 (`.data`) |
| 428 | `kPsDirtyRegEnd` | `0x00c5efe0` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x1000 (`.data`) |

### `client/warcraftxl/include/wxl/offsets/engine/GxDevice.hpp`

| line | constant | address | where |
|---|---|---|---|
| 62 | `kBaseVTable` | `0x00a2ddc0` | `.rdata` item (string / vtable / literal), untagged; between `CGxDevice::s_texFormatBytesPerBlock` 0xa2db60 and `s_glBufferTarget` 0xa2e000 |
| 65 | `kD3d9ExVTable` | `0x00a2f500` | `.rdata` item (string / vtable / literal), untagged; between `s_d3dQueryTypes` 0xa2f424 and `CGxDeviceD3d::s_tolerableTexFmtMapping` 0xa2f7e8 |
| 79 | `kPureCall` | `0x0040baa5` | **outside any reconstructed function**: the CRT `_purecall` in the 0x0040B7F0..0x0041D0B0 region, which the decompilation covers only as `WinMain`/`MainFiberProc` (no `int3` padding separates them, so a linear sweep reports it as inside `WinMain`; it is not). |
| 808 | `kUiShaderName` | `0x009e3034` | `.rdata` item (string / vtable / literal), untagged; between `s_miniTextureInfo` 0x9e2e2c and `s_manifestZones` 0x9e4db8 |

### `client/warcraftxl/include/wxl/offsets/engine/Io.hpp`

| line | constant | address | where |
|---|---|---|---|
| 56 | `kRequiredArchiveGateJnz` | `0x004060bd` | inside `OpenArchives` +0x2ed (instruction) |
| 106 | `kExpansionFlagJe` | `0x004062c9` | inside `OpenArchives` +0x4f9 (instruction) |

### `client/warcraftxl/include/wxl/offsets/engine/Liquid.hpp`

| line | constant | address | where |
|---|---|---|---|
| 104 | `kSettingsBankRows` | `0x00d43b1c` | inside static `Liquid::CSettingsBank::s_settings` 0xd43b14 +0x8 (`.data`) |
| 105 | `kSettingsBankCount` | `0x00d43b18` | inside static `Liquid::CSettingsBank::s_settings` 0xd43b14 +0x4 (`.data`) |
| 223 | `kDnBandColors` | `0x00d38bd4` | inside static `DayNight::g_dnInfo` 0xd38b00 +0xd4 (`.data`) |

### `client/warcraftxl/include/wxl/offsets/engine/Shader.hpp`

| line | constant | address | where |
|---|---|---|---|
| 104 | `kConstCacheVertex` | `0x00c5efe8` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x1008 (`.data`) |
| 108 | `kConstDirtyPixel` | `0x00c5efe0` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x1000 (`.data`) |
| 109 | `kConstDirtyVertex` | `0x00c5ffe8` | inside static `CGxDevice::s_shadowConstants` 0xc5dfe0 +0x2008 (`.data`) |
| 209 | `kCgxShaderRtti` | `0x00ad8ca8` | inside static `s_formatNames` 0xad8c80 +0x28 (`.data`) |
| 217 | `kExteriorEffectOpaque` | `0x00d1c400` | inside static `CMapObj::s_classicShaders` 0xd1c3f0 +0x10 (`.data`) |
| 221 | `kExteriorEffectComposite` | `0x00d1c408` | inside static `CMapObj::s_classicShaders` 0xd1c3f0 +0x18 (`.data`) |
| 229 | `kAltEffectComposite` | `0x00d1c3ec` | inside static `CMapObj::s_unifiedShaders` 0xd1c3d4 +0x18 (`.data`) |

### `client/warcraftxl/include/wxl/offsets/engine/Sky.hpp`

| line | constant | address | where |
|---|---|---|---|
| 57 | `kFogColor` | `0x00d38bf4` | inside static `DayNight::g_dnInfo` 0xd38b00 +0xf4 (`.data`) |
| 62 | `kSkyDrawGate` | `0x00d38ccc` | inside static `DayNight::g_dnInfo` 0xd38b00 +0x1cc (`.data`) |

### `client/warcraftxl/include/wxl/offsets/engine/Sound.hpp`

| line | constant | address | where |
|---|---|---|---|
| 48 | `kSoundGroupArrayPtr` | `0x00d438fc` | inside static `s_channelGroups` 0xd438f4 +0x8 (`.data`) |

### `client/warcraftxl/include/wxl/offsets/engine/Zlib.hpp`

| line | constant | address | where |
|---|---|---|---|
| 29 | `kInflateInit` | `0x00865150` | **unreconstructed function** (`55 8B EC` prologue after `C3 CC CC` padding). The whole zlib block between `luaK_patchlist` 0x00863580 and `OsGetPhysicalCursorPos` 0x00868C10 has no rows: zlib is not in the decompilation. |
| 33 | `kInflate` | `0x00865270` | **unreconstructed function** (`55 8B EC` after a `ret`), same zlib gap. |
| 35 | `kInflateEnd` | `0x00866660` | **unreconstructed function** (`55 8B EC`), same zlib gap. |

### `client/warcraftxl/include/wxl/offsets/game/ADT.hpp`

| line | constant | address | where |
|---|---|---|---|
| 42 | `kLiquidRowFlagTest` | `0x007c846c` | inside `CMapObjGroup::QueryLiquid` +0x10c (instruction) |
| 47 | `kLiquidTypeDbRows` | `0x00ad4084` | inside static `g_liquidTypeDB` 0xad4064 +0x20 (`.data`) |
| 48 | `kLiquidTypeDbMinId` | `0x00ad4074` | inside static `g_liquidTypeDB` 0xad4064 +0x10 (`.data`) |
| 49 | `kLiquidTypeDbMaxId` | `0x00ad4070` | inside static `g_liquidTypeDB` 0xad4064 +0xc (`.data`) |
| 87 | `kChunkFrustumCullRetB` | `0x00799e65` | inside `CWorldScene::CullChunks` +0x125 (instruction) |
| 395 | `kVsConstC18` | `0x00d251c0` | inside static `CMapRenderChunk::s_vertexShaderConsts` 0xd250a0 +0x120 (`.data`) |

### `client/warcraftxl/include/wxl/offsets/game/CharCreate.hpp`

| line | constant | address | where |
|---|---|---|---|
| 70 | `kPreferenceInitLengthSite` | `0x004e1c34` | inside `CCharacterCreation::Initialize` +0x14 (operand) |
| 75 | `kPreferenceFreeRaceCountSite` | `0x004e1e9b` | inside `CCharacterCreation::Shutdown` +0x7b (operand) |

### `client/warcraftxl/include/wxl/offsets/game/DB2.hpp`

| line | constant | address | where |
|---|---|---|---|
| 30 | `kMaxId` | `0x00ad3d58` | inside static `g_itemDB` 0xad3d4c +0xc (`.data`) |
| 31 | `kMinId` | `0x00ad3d5c` | inside static `g_itemDB` 0xad3d4c +0x10 (`.data`) |
| 32 | `kIdTable` | `0x00ad3d6c` | inside static `g_itemDB` 0xad3d4c +0x20 (`.data`) |
| 42 | `kStorageLoadedFlag` | `0x00ad4164` | inside static `g_mapDB` 0xad4160 +0x4 (`.data`) |
| 43 | `kRecordCount` | `0x00ad4168` | inside static `g_mapDB` 0xad4160 +0x8 (`.data`) |
| 44 | `kMaxId` | `0x00ad416c` | inside static `g_mapDB` 0xad4160 +0xc (`.data`) |
| 45 | `kMinId` | `0x00ad4170` | inside static `g_mapDB` 0xad4160 +0x10 (`.data`) |
| 46 | `kRecordData` | `0x00ad417c` | inside static `g_mapDB` 0xad4160 +0x1c (`.data`) |
| 47 | `kIdTable` | `0x00ad4180` | inside static `g_mapDB` 0xad4160 +0x20 (`.data`) |
| 57 | `kRecordCount` | `0x00ad4168` | inside static `g_mapDB` 0xad4160 +0x8 (`.data`) |
| 58 | `kRecordData` | `0x00ad417c` | inside static `g_mapDB` 0xad4160 +0x1c (`.data`) |
| 61 | `kIdTable` | `0x00ad4180` | inside static `g_mapDB` 0xad4160 +0x20 (`.data`) |
| 67 | `kRecordCount` | `0x00ad3334` | inside static `g_charSectionsDB` 0xad332c +0x8 (`.data`) |
| 68 | `kRecordData` | `0x00ad3348` | inside static `g_charSectionsDB` 0xad332c +0x1c (`.data`) |
| 83 | `kRecordCount` | `0x00ad3508` | inside static `g_creatureModelDataDB` 0xad3500 +0x8 (`.data`) |
| 84 | `kRecordData` | `0x00ad351c` | inside static `g_creatureModelDataDB` 0xad3500 +0x1c (`.data`) |
| 98 | `kRecordCount` | `0x00ad34c0` | inside static `g_creatureDisplayInfoDB` 0xad34b8 +0x8 (`.data`) |
| 99 | `kRecordData` | `0x00ad34d4` | inside static `g_creatureDisplayInfoDB` 0xad34b8 +0x1c (`.data`) |
| 116 | `kMinId` | `0x00ad3438` | inside static `g_chrRacesDB` 0xad3428 +0x10 (`.data`) |
| 117 | `kMaxId` | `0x00ad3434` | inside static `g_chrRacesDB` 0xad3428 +0xc (`.data`) |
| 118 | `kIdTable` | `0x00ad3448` | inside static `g_chrRacesDB` 0xad3428 +0x20 (`.data`) |
| 129 | `kMaxId` | `0x00ad3de8` | inside static `g_itemDisplayInfoDB` 0xad3ddc +0xc (`.data`) |
| 130 | `kMinId` | `0x00ad3dec` | inside static `g_itemDisplayInfoDB` 0xad3ddc +0x10 (`.data`) |
| 131 | `kIdTable` | `0x00ad3dfc` | inside static `g_itemDisplayInfoDB` 0xad3ddc +0x20 (`.data`) |

### `client/warcraftxl/include/wxl/offsets/game/Dbc.hpp`

| line | constant | address | where |
|---|---|---|---|
| 99 | `kLoadDispatch` | `0x006348b0` | **unreconstructed thunk** (`8B 01 FF 60 04` = `mov eax,[ecx]; jmp [eax+4]`, 5 bytes between `TSFixedArray<T>::ReallocData` and `LocateWeaponSubclass`). The generator has no row because the source writes it as a virtual call. |
| 104 | `kLoadDispatchSlot` | `0x00634e2b` | inside `ClientDBInitialize` +0x2b (operand) |
| 132 | `kEmptyString` | `0x009e14ff` | `.rdata` item (string / vtable / literal), untagged; between `s_signatureExponent` 0x9e0da4 and `s_wowArgList` 0x9e1be0 |
| 145 | `kLoadVtableSlot` | `0x00a28c54` | `.rdata` item (string / vtable / literal), untagged; between `s_primaryStatOrder` 0xa262f0 and `s_inventoryTypeSlots` 0xa2d288 |
| 182 | `kFileColumnsCmpSlot` | `0x00650a6e` | inside `WowClientDB_Common<T>::Load` +0x12e (operand) |
| 183 | `kFileColumnsExpectedSlot` | `0x00650a75` | inside `WowClientDB_Common<T>::Load` +0x135 (operand) |
| 184 | `kRecordSizeCmpSlot` | `0x00650ac7` | inside `WowClientDB_Common<T>::Load` +0x187 (operand) |
| 185 | `kRecordSizeExpectedSlot` | `0x00650ace` | inside `WowClientDB_Common<T>::Load` +0x18e (operand) |

### `client/warcraftxl/include/wxl/offsets/game/GameObject.hpp`

| line | constant | address | where |
|---|---|---|---|
| 52 | `kDisplayInfoRows` | `0x00ad3958` | inside static `g_gameObjectDisplayInfoDB` 0xad3938 +0x20 (`.data`) |
| 53 | `kDisplayInfoMinId` | `0x00ad3948` | inside static `g_gameObjectDisplayInfoDB` 0xad3938 +0x10 (`.data`) |
| 54 | `kDisplayInfoMaxId` | `0x00ad3944` | inside static `g_gameObjectDisplayInfoDB` 0xad3938 +0xc (`.data`) |

### `client/warcraftxl/include/wxl/offsets/game/GroundEffect.hpp`

| line | constant | address | where |
|---|---|---|---|
| 101 | `kDistCapFloat` | `0x009e8cfc` | `.rdata` item (string / vtable / literal), untagged; between `s_defaultSeed` 0x9e8a9c and `s_argList` 0x9e9cb0 |
| 104 | `kDensityClampImms` | `0x007b2aa6` | inside `CDetailDoodad::UpdatePools` +0x26 (operand) |
| 104 | `kDensityClampImms` | `0x007b2aba` | inside `CDetailDoodad::UpdatePools` +0x3a (operand) |
| 104 | `kDensityClampImms` | `0x007b2ac5` | inside `CDetailDoodad::UpdatePools` +0x45 (operand) |
| 104 | `kDensityClampImms` | `0x007b2acc` | inside `CDetailDoodad::UpdatePools` +0x4c (operand) |

### `client/warcraftxl/include/wxl/offsets/game/M2.hpp`

| line | constant | address | where |
|---|---|---|---|
| 33 | `kInit` | `0x0083cf00` | **unreconstructed function** (`53 8B D9` after `CC` padding), between `CM2Shared::Initialize` 0x0083CC80 and `TSList::Clear` 0x0083D2D0. It IS in the source: `int32_t CM2Shared::FinishLoading()` at `experiments/WarcraftXL/client/Engine/Source/Model2/M2Shared.cpp:1729` carries no `OFFSET:` tag. Identity confirmed by `CM2Shared::LoadSucceededCallback` (0x0083D340, line 1765): `AsyncFileReadDestroyObject(m_asyncObject); m_asyncObject = nullptr; FinishLoading();`, exactly the body the hand comment on `kLoadSucceededCallback` describes calling `kInit`. |
| 66 | `kVersionGateInit` | `0x0083cf51` | inside the untagged `CM2Shared::FinishLoading` (0x0083CF00, see `kInit`) +0x51, a `ja`: a version-gate branch of the loader, as the comment says. |
| 71 | `kVersionGateAnim` | `0x0083c745` | inside `CM2Shared::FinishLoadingLowPrioritySequence` +0x65 (instruction) |
| 247 | `struct row` | `0x0083afba` | inside `M2Init<M2Particle>` +0x2a (instruction) |
| 249 | `struct row` | `0x0083afe7` | inside `M2Init<M2Particle>` +0x57 (instruction) |
| 252 | `struct row` | `0x00832f18` | inside `CM2Model::InitializeLoaded` +0x78 (instruction) |
| 254 | `struct row` | `0x00834124` | inside `CM2Model::InitializeLoaded` +0x1284 (instruction) |
| 256 | `struct row` | `0x008309ee` | inside `CM2Model::AnimateParticleST` +0x2e (instruction) |
| 258 | `struct row` | `0x0082d6ba` | inside `CM2Model::AnimateParticlesMT` +0x3ca (instruction) |
| 263 | `struct row` | `0x00822cfb` | inside `CM2Scene::Animate` +0x12db (instruction) |
| 265 | `struct row` | `0x0082536e` | inside `CM2Model::ReplaceTexture` +0x10e (instruction) |
| 267 | `struct row` | `0x00825470` | inside `CM2Model::ReplaceParticleColor` +0x60 (instruction) |
| 296 | `kParticleTexIdInitLoaded` | `0x00833ed9` | inside `CM2Model::InitializeLoaded` +0x1039 (instruction) |
| 298 | `kParticleTexIdReplaceTex` | `0x00825349` | inside `CM2Model::ReplaceTexture` +0xe9 (instruction) |
| 532 | `kTexPivotX` | `0x00d411d0` | inside static `CM2Model::s_sequenceCallbacks` 0xd411c0 +0x10 (`.data`) |
| 533 | `kTexPivotY` | `0x00d411d4` | inside static `CM2Model::s_sequenceCallbacks` 0xd411c0 +0x14 (`.data`) |
| 534 | `kTexPivotZ` | `0x00d411d8` | inside static `CM2Model::s_sequenceCallbacks` 0xd411c0 +0x18 (`.data`) |
| 535 | `kTexPivotFlag` | `0x00d411dc` | inside static `CM2Model::s_sequenceCallbacks` 0xd411c0 +0x1c (`.data`) |
| 541 | `kPaletteCallRetSceneA` | `0x00821b53` | inside `CM2Scene::Animate` +0x133 (instruction) |
| 542 | `kPaletteCallRetSceneB` | `0x00821bdd` | inside `CM2Scene::Animate` +0x1bd (instruction) |
| 543 | `kPaletteCallRetWorker` | `0x0081ceff` | inside `CM2Scene::AnimateThread` +0x8f (instruction) |
| 563 | `kDoodadDrainRetA` | `0x00821c77` | inside `CM2Scene::Animate` +0x257 (instruction) |
| 564 | `kDoodadDrainRetB` | `0x00822ab5` | inside `CM2Scene::Animate` +0x1095 (instruction) |
| 1956 | `kSetModelIndicesSrcCopy` | `0x00829091` | inside `CM2Model::SetIndices` +0x101 (instruction) |
| 1957 | `kSetModelIndicesSrcRebase` | `0x008290bc` | inside `CM2Model::SetIndices` +0x12c (instruction) |
| 1970 | `kSharedSetIndicesSrc` | `0x0083619f` | inside `CM2Shared::SetIndices` +0xff (instruction) |
| 1990 | `kDrawBatchStartIndexSites` | `0x008205dd` | inside `CM2SceneRender::DrawBatch` +0x22d (instruction) |
| 1990 | `kDrawBatchStartIndexSites` | `0x00820666` | inside `CM2SceneRender::DrawBatch` +0x2b6 (instruction) |
| 1990 | `kDrawBatchStartIndexSites` | `0x008206de` | inside `CM2SceneRender::DrawBatch` +0x32e (instruction) |

### `client/warcraftxl/include/wxl/offsets/game/WMO.hpp`

| line | constant | address | where |
|---|---|---|---|
| 67 | `kRootCreateData` | `0x007d7eb0` | **unreconstructed function** (`56 57 8B F1` right after the `ret 4` of `CMapObjGroup::CreateOptionalDataPointers`, before `CreateDataPointers` 0x007D7F50). In the source its body is folded into `CMapObj::PostloadCallback` (`experiments/WarcraftXL/client/WoW/Source/WorldClient/MapObjRead.cpp:413-431`: `Load(); CreateMaterials();` ambient colour, bbox, group count, `AllocMapObjGroup` per group, `m_isGroupLoaded = 1`), which matches the hand comment word for word. The binary keeps it as a separate function that the decompiler inlined into its caller. |
| 371 | `kOutdoorEnabled` | `0x00adf59c` | inside static `CWorldScene::frustumPortalView` 0xadf58c +0x10 (`.data`) |

### `client/warcraftxl/include/wxl/offsets/game/World.hpp`

| line | constant | address | where |
|---|---|---|---|
| 39 | `kFocusPosY` | `0x00cd777c` | inside static `World::s_currentWorldPos` 0xcd7778 +0x4 (`.data`) |
| 40 | `kFocusPosZ` | `0x00cd7780` | inside static `World::s_currentWorldPos` 0xcd7778 +0x8 (`.data`) |
| 46 | `kPurgeReloadFlag` | `0x00cd767c` | inside static `CWorld::status` 0xcd7678 +0x4 (`.data`) |
| 67 | `kActiveListHead` | `0x00adfbf4` | inside static `CMap::s_unk_ADFBEC` 0xadfbec +0x8 (`.data`) |
| 130 | `kVisibleChunkHead` | `0x00cdaf68` | inside static `CWorldScene::sortTable` 0xcd9048 +0x1f20 (`.data`) |

## Signature disagreements

232 `using ...Fn = ...` typedefs were paired with the constant they sit next to and compared with the decompiled definition (parameter count and types, member vs static, calling convention). Where a member's static-ness mattered it was checked in the class header; where the convention mattered it was read from the bytes. Everything not listed here agrees. In order of seriousness:

| # | typedef (header:line) | constant | generated definition | disagreement | severity |
|---|---|---|---|---|---|
| 1 | `IntersectFn = int(__cdecl*)(const void* rayStart, const void* rayEnd, uint32_t flags, void* result6)` (`game/World.hpp:229`) | `kIntersectWrapper = 0x004F9930` | `int32_t CGWorldFrame::HitTest(const C3Vector& start, const C3Vector& end, uint32_t flags, HITTESTRESULT* result)`, non-static member (`WorldFrame.h:192`) | The bytes say `__thiscall`: `mov edi, ecx` at 0x004F9954 and `ret 0x10` at 0x004F9C63 (callee pops the four arguments). Called through the cdecl typedef, `this` is never passed and ESP ends up 16 bytes off after the call. The comment ("this = world frame") knows it; the typedef does not. | **Serious.** Latent: no caller in the core today (`wxl/game/Pick.hpp` uses `WorldIntersectFn`, which is correct: six `__cdecl` args match `World::Intersect`). |
| 2 | `World_EnterFn = void(__cdecl*)(int worldTime, int withLoadingScreen)` (`game/World.hpp:36`) | `kEnter = 0x00781500` | `void World::Preload(const C3Vector& position, int32_t showLoading)` | Argument 1 is a pointer to the position to preload around, not a time. Count and convention agree, and the core's detour (`src/client/CWorldScene/World.cpp:47`) forwards it opaquely, so it works; the documentation misleads anyone who calls it. | Medium (type). |
| 3 | `EngineInitFn = uint32_t(__cdecl*)()` (`engine/Boot.hpp:32`) | `kEngineInit = 0x004047E0` | `int32_t InitializeEngineCallback(const void* data, void* param)` | Two parameters vs none. `__cdecl`, and the body reads neither (no `[ebp+8]`/`[esp+...]` access before the `ret` at 0x00404922), so a detour that calls through with no arguments is harmless. | Low. |
| 4 | `Wmo_CullSortTableFn = void(__cdecl*)(void* rect, int restrictedToPortalRect)` (`game/WMO.hpp:436`) | `kCullSortTable = 0x0079A790` | `void CWorldScene::CullSortTable(CRect* rect)` | One parameter: the body reads only `[ebp+8]`. Harmless for a cdecl call; a detour with this signature reads a junk second argument. | Low. |
| 5 | `World_RenderFn = void(__cdecl*)(const float* viewerPos, uint32_t flags)` (`game/World.hpp:152`) | `kRender = 0x0077EFF0` | `void CWorld::Render(const C3Vector& cameraPos, float time)` | Argument 2 is a float time, not a flag word. Same size, same convention. | Low (type). |
| 6 | `LiquidRenderPassFn = void(__fastcall*)(void* bank, void* edx, void* transform, int passType)` (`engine/Gx.hpp:335`) | `kLiquidRenderPass = 0x008A2240` | `void Liquid::CRenderer::Flush(const C3Vector& cameraPos, uint32_t pass)` | `this` is the renderer (`CWorldScene::s_liquidRenderer`), not the bank; the first stack argument is the camera position, not a transform. Shape agrees. | Low (naming). |
| 7 | `GxDeviceDrawFn = void(__fastcall*)(void* device, void* edx, uint32_t* batch, int indexed)` (`engine/Gx.hpp:205`) | `kGxDeviceDraw = 0x006A3620` | `void CGxDeviceD3d::Draw(CGxBatch* batch, int32_t count)` | Second argument named `indexed` vs `count`; `ret 8` confirms the shape. One of the two names is wrong; not checked further. | Low (naming). |
| 8 | `SimpleTextureDestructorFn = void*(__fastcall*)(void* texture, void* edx, unsigned int flags)` (`engine/Layout.hpp:98`) | `kSimpleTextureDestructor = 0x00483010` | `CSimpleTexture::~CSimpleTexture()` [auto] | The hand side is right and the generated side is imprecise: the bytes end in `ret 4` (0x0048305D), so 0x00483010 is the scalar deleting destructor with its flag argument, which the source can only express as the plain destructor. | Generated-side naming only. |

Checked and agreeing, for the record: the whole Lua C API block (`engine/Lua.hpp`, 40 cdecl entries), the `STORMAPI`/`__stdcall` storage and string entries (`SFile::OpenEx`, `SFileReadFile`, `SFile::SetFilePointer`, `SFileGetFileSize`, `SFileCloseFile`, `SStrDupA`, `SMemAlloc`, `SMemFree`, `SStrToFloat`), the CGxDevice member set (`__fastcall` with `this`), the static members typed `__cdecl` (`FrameTime::Update`, `CShaderEffect::UpdateProjMatrix`, `CShadowCache::GetShadowValue`, `FrameScript_Object::FillScriptMethodTable`, `CMap::LoadTexture`, `CMapArea::AllocAsyncLoadBuffer`, `CMapDoodadDef::Purge`, `CMapObj::AllocOccluder`, `CMapObj::CullBatch`, `CMapRenderChunk::SetVertexShader`, `CGWorldFrame::GetActiveCamera`, `Liquid::CMaterialBank::GetMaterial`, all declared `static` or shown by the bytes to use no `this`), `CGxDevice::PrimCalcCount` (`ret 8`, static `__stdcall` as documented), the struct-returning operators (`C44Matrix operator*`, `C3Vector operator*`, `C44Matrix::Inverse`, hidden return pointer first, as the typedefs say), and the WMO/ADT/M2/particle member entries (`__thiscall` with the stated stack-argument counts).

## What made the audit hard, and what a by-name migration has to handle

1. **The generated name column is not a key.** 18 generated names are reached from several hand addresses because they are overloads or template instantiations: `CMapObjGroup::GetTris` (0x007CB0C0, 0x007CB180, 0x007CB7B0), `CMapChunk::Intersect` (three), `CMap::CreateDoodadDef` (MDDF vs MODD), `CMap::CreateMapObjDef`, `CMapChunk::CreateIndices` / `CreateVerticesWorld` / `CreateVerticesLocal` (high/low pairs), `CMapObj::GetTris`, `CMapObj::QueryLighting`, `CMapObj::DistFromClosestExtPortal`, `CMapObjGroup::GetLiquidTris`, `CM2Model::SetWorldTransform`, `CM2Shared::FinishLoadingLowPrioritySequence`, `TextureCreate` (by name vs by size), `operator*` (matrix*matrix vs vector*matrix), `WowClientDB_Common<T>::Load` and `WowClientDB_RLE<T>::GetRecord` (distinguished only by the `targs` column: `MapRec` / `SpellRec` / `ItemDisplayInfoRec`), and `M2Init` (six hand constants `kReadVertices`, `kReadByteArray`, `kReadVector3`, `kReadInt32Array`, `kReadInt16Array`, `kReadSkinTextureUnits` all land on rows named plain `M2Init` with an empty `targs`, because the template tag lists addresses without arguments). A reference by name needs a disambiguator: `targs` where it exists, otherwise the signature or the address itself.

2. **Hand names are purpose names, generated names are symbol names, and the two sometimes cross.** `kCharPaste = 0x004EF9D0` is the generated `PasteScale` and `kCharPasteScale = 0x004EC550` is the generated `Paste`; both comments describe their bodies correctly (magnifying copy vs reducing copy with a source-level argument), so neither is wrong, but a migration that trusts the suffix will swap them. The same family: `kRootCollectTrisAlt`, `kGroupCollectTrisAlt`, `kGroupLiquidTrisAlt` are overloads the generated table cannot tell apart by name.

3. **Generated-side naming defects to expect.** Four rows carry a mis-nested qualification, `CM2Shared::CancelAllDeferredSequences::CM2Shared::~CM2Shared` and siblings (`LoadLowPrioritySequence`, `LowPrioritySequenceLoadedCallback`, `Release`), hit by `kSharedDestroy`, `kLoadLowPrioritySequence`, `kAnimLoadComplete`, `kSharedRelease`. `CWorldScene::s_fogPermute` is declared in `MapObj.cpp`. Several statics are `s_unk_<address>` (`CMap::s_unk_ADFBEC`). `CSimpleTexture::~CSimpleTexture` names the deleting destructor. A destructor-named row and a deleting-destructor address are the same thing to the generator and different things to a hook.

4. **The same address under two hand names, in two headers.** `0x00681F20` (`gx::kTextureUpdate`, `gxdevice::kGxTexUpdate`), `0x0076E5A0` (`io::kSMemFree`, `mem::kFree`), `0x00872C10` (`gx::kShaderUpdateProjMatrix`, `shader::kEffectUpdateProjMatrix`), `0x00873BA0` (`m2::kPushAlphaRef`, `shader::kEffectSetAlphaRef`), `0x00C5DFE0` (`gx::kPsConstCache`, `shader::kConstCachePixel`) plus the three dirty-range/vertex-cache twins at `0x00C5EFE0/0x00C5EFE8/0x00C5FFE8`. Cross-namespace aliases exist too (`adt::kTexResolve = engine::sky::kTextureGetGxTex`, `m2::kSMemAlloc = engine::mem::kAlloc`, `adt::kSetSamplerTexture = engine::shader::kGxStateSet`, `m2::kVsConstC31 = engine::gx::kVsConstCache + 0x1F0`, `shader::kConstDirtyPixel = kConstCachePixel + kConstCacheBytes`). A by-name table should have one owner per symbol and aliases that point at it.

5. **Hand names are reused across namespaces.** `kInitialize` x4, `kUpdate` x3, `kRender` x3, `kShutdown`, `kMapUnload`, `kPrepareUpdate` x2 each, and in `game/DB2.hpp` the per-table blocks repeat `kStorageObject` (x6), `kRecordCount`, `kRecordData`, `kIdTable`, `kMinId`, `kMaxId` in nested namespaces. Any flat name -> row map needs the namespace in the key.

6. **A third of the "absent" rows are not symbols at all and never will be.** 45 rows are mid-function sites: return addresses used as cadence keys (`kPaletteCallRetSceneA/B`, `kPaletteCallRetWorker`, `kDoodadDrainRetA/B`), immediates to patch (`kDensityClampImms[4]`, `kFileColumnsCmpSlot`, `kRecordSizeExpectedSlot`, `kMipScratchDimWImm`, `kPreferenceInitLengthSite`), branch sites (`kVersionGateInit`, `kExpansionFlagJe`, `kRequiredArchiveGateJnz`), displacement operands (`kBlendSrcReadDisp`), read sites (`kSetModelIndicesSrcCopy`, `kDrawBatchStartIndexSites[3]`, the nine M2 struct rows). They can only be expressed as `symbol + delta` (e.g. `CM2Scene::Animate + 0x133`), which the generated table supports only if function extents are added to it; today it has starts only, and the owner had to be recovered by disassembling Wow.exe.

7. **Likewise for data.** 59 rows are fields inside a generated static: the DBC store fields (`+0x08 recordCount`, `+0x0C maxId`, `+0x10 minId`, `+0x1C recordData`, `+0x20 idTable` inside `g_itemDB`, `g_mapDB`, `g_charSectionsDB`, `g_creatureModelDataDB`, `g_creatureDisplayInfoDB`, `g_chrRacesDB`, `g_itemDisplayInfoDB`, `g_gameObjectDisplayInfoDB`, `g_liquidTypeDB`), fields of `DayNight::g_dnInfo` (`kFogColor`, `kSkyDrawGate`, `kDnBandColors`), the halves of `CGxDevice::s_shadowConstants[2]` (`kVsConstCache`, the four dirty-range words), `CWorld::status` (`kPurgeReloadFlag`), `World::s_currentWorldPos` (`kFocusPosY/Z`), `CMapRenderChunk::s_vertexShaderConsts` (`kVsConstC18`), `CMapObj::s_classicShaders[4]/[6]`, `s_unifiedShaders[6]`, `Liquid::CSettingsBank::s_settings` (+4, +8), `CWorldScene::sortTable` (`kVisibleChunkHead`). They want `static + offset` or, better, the struct member name. Note `kOutdoorEnabled = 0x00ADF59C` sits inside `CWorldScene::frustumPortalView` (a `CPortalView`, +0x10): the hand table and the decompilation disagree on whether that float is its own global.

8. **`.rdata` is only partly tagged (18 rows).** `kAddonPublicKey` (`InterfaceKey`) has a row; the five vtables (`kGxDeviceVTable`, `kBaseVTable`, `kD3d9ExVTable`, `kSimpleCameraVTable`, `kTexHandleVTable`), the eleven string literals of `engine/Addon.hpp` and `game/Dbc.hpp`, `kUiShaderName`, `kDistCapFloat` and `kLoadVtableSlot` do not. Those stay literals, or the generator learns to tag vtables and string pools.

9. **Seven absent code addresses (five functions, a thunk and the CRT) are code the decompilation has not reconstructed the decompilation has not reconstructed**: the three zlib entries (`kInflateInit`, `kInflate`, `kInflateEnd`: the whole zlib region has no rows), `m2::kInit = CM2Shared::FinishLoading` (present in the source at `M2Shared.cpp:1729` but without an `OFFSET:` tag, so the generator cannot see it), `wmo::kRootCreateData` (inlined by the decompiler into `CMapObj::PostloadCallback`), `dbc::kLoadDispatch` (a 5-byte virtual-call thunk) and `gxdevice::kPureCall` (CRT). The first two are fixable upstream with a tag; the others need to stay literals.

10. **Comment attachment is irregular.** The statement of intent is sometimes a block above a group of constants (so the first constant's comment describes the whole block and the rest have none), sometimes a trailing `//`, sometimes `///`, and in a few places it has drifted from the constant it precedes (`worldscene::kMulVecMatrix`'s block describes `wmo::kGroupResidentAccessor`; `addon::kSha1Prepare`'s block describes `kSignatureVerify`; the `kDrawBatchStartIndexSites` block is split across an array). A migration that wants to carry the intent along as a per-row doc string will have to re-attach by hand in those places.

11. **Values that are not literals.** `kConstDirtyPixel`, `kConstDirtyVertex`, `kVsConstC31` and the slot indices `kGxSetProjectionSlot = 0xA0 / 4` etc. are expressions over other constants; `kGroupSlotMotv2`/`kGroupSlotMocv2` are brace initialisers; `game/CharCreate.hpp:55-62` and `game/ADT.hpp:260` are arrays of sites and of draw entries (the ADT one is six CONFIRMED function starts). The extraction had to special-case each.

## Field-offset inventory (not audited)

804 `constexpr size_t` constants (the headers' convention for struct field offsets, sizes and vtable slot indices). The other small-valued constants ({'uint8_t': 10, 'uint32_t': 140, 'unsigned': 77, 'uintptr_t': 40, 'int32_t': 2, 'int': 17}) are masks, flags, enum values, counts and slot indices, not offsets, and are left out. The owner column is the nearest `// --- section ---` heading above the constant plus, when the trailing comment says `X -> field`, that `X`; it is a hint for the later verification pass, not a claim.


### `client/warcraftxl/include/wxl/offsets/engine/Addon.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 52 | `kSignatureFileSize` | `0x114` | the .sig file | --- the scheme --------------------------------------------------------------------------- |
| 53 | `kSignatureDigestOffset` | `0x0` | the .sig file |  |
| 54 | `kSignatureMagicOffset` | `0x10` | the .sig file |  |
| 55 | `kSignatureBlockOffset` | `0x14` | the .sig file |  |
| 67 | `kAddonPublicKeySize` | `0x100` | the key |  |
| 109 | `kSignatureCtxBlobSize` | `0x104` | SSignature | 256 + the magic |
| 110 | `kSignatureCtxBlobPtr` | `0x10` | SSignature | byte offset of [4] within the context |

### `client/warcraftxl/include/wxl/offsets/engine/Camera.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 46 | `kCameraPosition` | `0x8` | the camera the world renderer reads | float[3] |
| 47 | `kCameraForward` | `0x14` | the camera the world renderer reads | float[3] |
| 48 | `kCameraRight` | `0x20` | the camera the world renderer reads | float[3] |
| 49 | `kCameraUp` | `0x2c` | the camera the world renderer reads | float[3] |
| 50 | `kCameraFov` | `0x40` | the camera the world renderer reads | float, full angle in radians |

### `client/warcraftxl/include/wxl/offsets/engine/Draw.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 34 | `kOffNodeLayerCount` | `0x9` |  | Render-node field: layer count (the draw-loop bound). |

### `client/warcraftxl/include/wxl/offsets/engine/Gx.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 28 | `kD3DDeviceField` | `0x397c` | graphics device | graphics device -> IDirect3DDevice9* |
| 31 | `kBackBufferField` | `0x3b3c` |  | cached back-buffer surface |
| 32 | `kDepthSurfaceField` | `0x3b40` |  | cached world depth surface |
| 73 | `kDefWindowWidth` | `0x170` | blend-state factor tables | defWindow + 0xC |
| 74 | `kDefWindowHeight` | `0x16c` | blend-state factor tables | defWindow + 0x8 |
| 75 | `kCurWindowWidth` | `0x180` | blend-state factor tables | curWindow + 0xC = live render width |
| 76 | `kCurWindowHeight` | `0x17c` | blend-state factor tables | curWindow + 0x8 = live render height |
| 77 | `kFormatWidth` | `0x1d0` | blend-state factor tables | active format width  (backbuffer px, unscaled) |
| 78 | `kFormatHeight` | `0x1d4` | blend-state factor tables | active format height (backbuffer px, unscaled) |
| 79 | `kViewportDirty` | `0xf6c` | blend-state factor tables | set to 1 to force a viewport recompute from curWindow |
| 80 | `kRtOverrideField` | `0x2918` | blend-state factor tables | non-zero while an offscreen RT override is active (not the backbuffer pass) |
| 83 | `kMasterEnableField` | `0x2758` | blend-state factor tables | Master switches, one bit per rendering category, read by the device's master-enable check. |
| 96 | `kContextField` | `0xf58` | blend-state factor tables | Engine device fields the backend-neutral frame hook reads. The context flag is non-zero wh |
| 97 | `kWindowField` | `0x3968` | blend-state factor tables |  |
| 121 | `kDeviceProjection` | `0xf88` | blend-state factor tables | Where the device keeps the matrices those two slots upload, so they can be read back and r |
| 122 | `kDeviceViewIndex` | `0x1af8` | blend-state factor tables |  |
| 123 | `kDeviceViewBase` | `0x1b00` | blend-state factor tables |  |
| 124 | `kDeviceViewStride` | `0x40` | blend-state factor tables |  |
| 168 | `kDrawBatchCtxModelField` | `0x60` | blend-state factor tables ; Draw context | Draw context -> the current INSTANCE, not the shared model: the draw reads its bone palett |
| 176 | `kDrawBatchCtxSectionField` | `0x90` | blend-state factor tables ; draw context | draw context -> copied M2SkinSection. TRAP: both kDrawTriangleBatch and kDrawBatchDoodad r |
| 182 | `kDrawBatchCtxElementField` | `0x50` | blend-state factor tables ; draw context | draw context -> current M2Element/batch record |
| 186 | `kM2ElementSectionField` | `0x2c` | blend-state factor tables | M2Element/batch-record (what kDrawBatchCtxElementField points at) -> its own, always-curre |
| 195 | `kM2ElementRunLengthField` | `0x1c` | blend-state factor tables ; record | M2Element/batch-record -> total requested co-instance count for this run. Read once by kDr |
| 207 | `kGxBatchPrimType` | `0x0` | blend-state factor tables | uint32 |
| 208 | `kGxBatchStartIndex` | `0x4` | blend-state factor tables | uint32 -- the one wide field on the path |
| 209 | `kGxBatchIndexCount` | `0x8` | blend-state factor tables | uint32 |
| 210 | `kGxBatchMinIndex` | `0xc` | blend-state factor tables | uint16 |
| 211 | `kGxBatchMaxIndex` | `0xe` | blend-state factor tables | uint16 |
| 212 | `kGxDeviceVertexStream` | `0x2870` | blend-state factor tables | -> the bound vertex stream buffer |
| 213 | `kGxBufStreamOffset` | `0x18` | blend-state factor tables | uint32, in bytes |
| 214 | `kGxBufStreamStride` | `0xc` | blend-state factor tables | uint32 |
| 217 | `kGxDeviceBaseVertexMode` | `0x224` | blend-state factor tables | / Non-zero selects a path that passes a base vertex of zero instead of deriving one, so th |
| 284 | `kTexHandleNameField` | `0x6c` | typed views over the device objects ; texture handle | texture handle -> stored name (capped 0x104) |
| 296 | `kTexHandleGxTex` | `0x44` | typed views over the device objects ; CTexture | CTexture -> CGxTex* (0 until loaded; 0 forever when atlas-packed) |
| 310 | `kMipTableSlots` | `0x10` | typed views over the device objects | upper bound on mip levels (real count <= 13) |

### `client/warcraftxl/include/wxl/offsets/engine/GxDevice.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 35 | `kD3d9ObjectSize` | `0x3ea4` | object | CGxDevice::NewD3d allocation |
| 36 | `kD3d9ExObjectSize` | `0x3f28` | object | CGxDevice::NewD3d9Ex allocation, the largest stock one |
| 106 | `kDefWindow` | `0x164` | fields | --- fields ------------------------------------------------------------------------------- |
| 107 | `kCurWindow` | `0x174` | fields | what CGxDevice::DeviceCurWindow returns |
| 111 | `kApi` | `0x1b4` | fields | 0 OpenGL, 1 D3D9, 2 D3D9Ex; the base ctor leaves 6 |
| 112 | `kFormat` | `0x1bc` | fields | the active CGxFormat, kFormatSize bytes |
| 115 | `kApiFlag228` | `0x228` | fields | Written by each backend's ctor: D3D 0 / 1, OpenGL 1 / 0. Meaning not read. |
| 116 | `kApiFlag22C` | `0x22c` | fields |  |
| 120 | `kBaseMipLevel` | `0x350` | fields | The lowest mip level the device uses, which DeviceSetBaseMipLevel sets: a texture's upload |
| 124 | `kGammaRamp` | `0x354` | fields | Gamma ramps, 3 x 256 WORD. The live one is what DeviceSetGamma writes; the desktop one is  |
| 125 | `kGammaRampDesktop` | `0x954` | fields |  |
| 126 | `kGammaRampBytes` | `0x600` | fields |  |
| 128 | `kWndProc` | `0xf54` | fields | the client WNDPROC the backend window proc chains to |
| 129 | `kContext` | `0xf58` | fields | non-zero while the device is usable |
| 130 | `kFramePoisoned` | `0xf5c` | fields | a buffer lock failed: draws skipped until ScenePresent clears it |
| 131 | `kSizeTracking` | `0xf60` | fields | set by a size event unless minimized |
| 132 | `kHasFocus` | `0xf64` | fields |  |
| 133 | `kFrameCounter` | `0xf68` | fields | presented frames, bumped by the base ScenePresent |
| 138 | `kViewportMinX` | `0xf70` | fields | gx::kViewportDirty (0xF6C) completes this block. The viewport as the engine states it: six |
| 139 | `kViewportMaxX` | `0xf74` | fields |  |
| 140 | `kViewportMinY` | `0xf78` | fields |  |
| 141 | `kViewportMaxY` | `0xf7c` | fields |  |
| 142 | `kViewportMinZ` | `0xf80` | fields |  |
| 143 | `kViewportMaxZ` | `0xf84` | fields |  |
| 147 | `kApiProjection` | `0xfc8` | fields | The projection in the API's own depth range, derived by the D3D XformSetProjection from th |
| 148 | `kApiProjectionDirty` | `0x19e4` | fields |  |
| 153 | `kMatrixStacks` | `0x1008` | fields | The 19 CGxMatrixStack objects: 0-7 the application texture matrices, 8 world, 9 projection |
| 154 | `kMatrixStackStride` | `0x118` | fields |  |
| 155 | `kMatrixStackTop` | `0x0` | fields | uint32 index, 0..3 |
| 156 | `kMatrixStackDirty` | `0x4` | fields | uint8 |
| 157 | `kMatrixStackBase` | `0x8` | fields | 4 x C44Matrix |
| 158 | `kMatrixStackFlags` | `0x108` | fields | uint32 per level |
| 174 | `kClipPlaneDirty` | `0x24d0` | fields | Six user clip planes of four floats, and the mask saying which were written. |
| 175 | `kClipPlanes` | `0x24d4` | fields |  |
| 179 | `kScissorDirty` | `0x2534` | fields | The scissor rect, normalized like the viewport, and its dirty flag. |
| 180 | `kScissorRect` | `0x2538` | fields |  |
| 184 | `kLights` | `0x2548` | fields | Four fixed-function light slots: {C4Vector position (w == 1) or direction, ambient, diffus |
| 185 | `kLightStride` | `0x48` | fields |  |
| 187 | `kLightPosition` | `0x0` | fields |  |
| 188 | `kLightAmbient` | `0x10` | fields |  |
| 189 | `kLightDiffuse` | `0x1c` | fields |  |
| 190 | `kLightSpecular` | `0x28` | fields |  |
| 191 | `kLightAttenuation` | `0x34` | fields |  |
| 192 | `kLightEnabled` | `0x40` | fields |  |
| 195 | `kShaderTables` | `0x2668` | fields | One TSHashTable<CGxShader> per EGxShTarget, where ShaderCreate interns by name. |
| 196 | `kShaderTableStride` | `0x28` | fields |  |
| 203 | `kPoolList` | `0x2760` | fields | TSList of every CGxPool: {link offset, terminator {prev, next}}. The base constructor (kDe |
| 204 | `kPoolListPrev` | `0x2764` | fields | terminator prev link: never 0 once constructed |
| 205 | `kPoolListNext` | `0x2768` | fields | terminator next link (low bit 1 = terminator) |
| 206 | `kLockedBuf` | `0x2778` | fields | + pool type * 4: the CGxBuf currently locked |
| 207 | `kStreamPools` | `0x2780` | fields | + pool type * 4: the two default stream pools |
| 208 | `kStreamBufs` | `0x2788` | fields | + pool type * 4: their default CGxBufs |
| 212 | `kAttribSlots` | `0x2790` | fields | The vertex attribute slots a draw reads: one descriptor per EGxVertexAttrib, the CGxBuf fe |
| 213 | `kAttribSlotStride` | `0x10` | fields |  |
| 214 | `kAttribSlotAttrib` | `0x0` | fields | EGxVertexAttrib |
| 215 | `kAttribSlotFormat` | `0x4` | fields | attribute data format |
| 216 | `kAttribSlotOffset` | `0x8` | fields | byte offset inside the vertex |
| 217 | `kAttribSlotStrideB` | `0xc` | fields | bytes between vertices |
| 220 | `kAttribEnabled` | `0x28a8` | fields | bit per attribute |
| 223 | `kAttribChanged` | `0x28ac` | fields |  |
| 224 | `kVertexFormat` | `0x28b0` | fields | EGxVertexBufferFormat, or kVertexFormatCustom |
| 225 | `kVertexBuf` | `0x28b4` | fields | the CGxBuf of a fixed format |
| 226 | `kVertexStride` | `0x28b8` | fields |  |
| 227 | `kIndexBuf` | `0x28bc` | fields |  |
| 228 | `kIndexBufChanged` | `0x28c0` | fields |  |
| 231 | `kEmergencyLockBlock` | `0x28c4` | fields | + pool type * 0x14: the EmergencyMem object IBufLock 0x68FB10 locks |
| 232 | `kEmergencyLock` | `0x28d4` | fields | + pool type * 0x14: that object's +0x10 "in use" byte, read by IBufUnlock |
| 237 | `kRsCount` | `0x28f0` | fields | uint32, = kRenderStateCount |
| 238 | `kRsTable` | `0x28f4` | fields | -> the state array |
| 239 | `kRsShadowCount` | `0x28fc` | fields |  |
| 240 | `kRsShadow` | `0x2900` | fields | -> the hardware shadow, kRsShadowStride each |
| 241 | `kRsStride` | `0x18` | fields |  |
| 242 | `kRsShadowStride` | `0x10` | fields |  |
| 243 | `kRsValue` | `0x0` | fields | 16 bytes: int, float, pointer, or a C3Vector |
| 244 | `kRsSavedDepth` | `0x10` | fields |  |
| 245 | `kRsDirty` | `0x14` | fields |  |
| 340 | `kBatchSize` | `0x10` | CGxBatch, the argument to the Draw slot | --- CGxBatch, the argument to the Draw slot ---------------------------------------------- |
| 341 | `kBatchPrim` | `0x0` | CGxBatch, the argument to the Draw slot | uint32, an EGxPrim above |
| 342 | `kBatchStart` | `0x4` | CGxBatch, the argument to the Draw slot | uint32, first index, relative to the bound index buffer |
| 343 | `kBatchCount` | `0x8` | CGxBatch, the argument to the Draw slot | uint32, index count -- vertex count when not indexed |
| 344 | `kBatchMinVert` | `0xc` | CGxBatch, the argument to the Draw slot | uint16 |
| 345 | `kBatchMaxVert` | `0xe` | CGxBatch, the argument to the Draw slot | uint16 |
| 387 | `kTexList` | `0x2904` | CGxBatch, the argument to the Draw slot | TSList of every CGxTex |
| 388 | `kTexListHead` | `0x290c` | CGxBatch, the argument to the Draw slot |  |
| 392 | `kRenderTargets` | `0x2910` | CGxBatch, the argument to the Draw slot | The render-target table, {CGxTex*, cube face, API surface} per EGxBuffer (0 colour, 1 dept |
| 393 | `kRenderTargetStride` | `0xc` | CGxBatch, the argument to the Draw slot |  |
| 394 | `kRenderTargetTex` | `0x0` | CGxBatch, the argument to the Draw slot |  |
| 395 | `kRenderTargetFace` | `0x4` | CGxBatch, the argument to the Draw slot |  |
| 396 | `kRenderTargetApi` | `0x8` | CGxBatch, the argument to the Draw slot |  |
| 402 | `kScreenshotPending` | `0x2934` | CGxBatch, the argument to the Draw slot | set by DeviceTakeScreenShot, cleared by ScenePresent |
| 403 | `kScreenshotWidth` | `0x2938` | CGxBatch, the argument to the Draw slot |  |
| 404 | `kScreenshotHeight` | `0x293c` | CGxBatch, the argument to the Draw slot |  |
| 406 | `kCursorVisible` | `0x2950` | CGxBatch, the argument to the Draw slot |  |
| 407 | `kCursorHardware` | `0x2954` | CGxBatch, the argument to the Draw slot | the format asked for one and the caps allow it |
| 408 | `kCursorHotspotX` | `0x2958` | CGxBatch, the argument to the Draw slot |  |
| 409 | `kCursorHotspotY` | `0x295c` | CGxBatch, the argument to the Draw slot |  |
| 410 | `kCursorImage` | `0x2960` | CGxBatch, the argument to the Draw slot | 32 x 32 uint32, what CursorLock returns |
| 411 | `kCursorTexture` | `0x3960` | CGxBatch, the argument to the Draw slot | the software cursor's CGxTex |
| 414 | `kWindow` | `0x3968` | CGxBatch, the argument to the Draw slot | HWND |
| 415 | `kWindowClassAtom` | `0x396c` | CGxBatch, the argument to the Draw slot | uint16, from kWindowClassCreate |
| 416 | `kOwnsWindow` | `0x3970` | CGxBatch, the argument to the Draw slot | 1 when the device created the window itself |
| 423 | `kD3dModule` | `0x3974` | CGxBatch, the argument to the Draw slot | HMODULE of d3d9.dll |
| 424 | `kD3d9` | `0x3978` | CGxBatch, the argument to the Draw slot | IDirect3D9* |
| 425 | `kD3dDevice` | `0x397c` | CGxBatch, the argument to the Draw slot | IDirect3DDevice9*; = gx::kD3DDeviceField |
| 426 | `kD3dCaps` | `0x3980` | CGxBatch, the argument to the Draw slot | D3DCAPS9, the backend's copy |
| 427 | `kD3dOffscreenDepth` | `0x3b38` | CGxBatch, the argument to the Draw slot | depth surface for colour targets without a depth texture (0x006A7940) |
| 428 | `kD3dBackBuffer` | `0x3b3c` | CGxBatch, the argument to the Draw slot | GetRenderTarget(0) at IStateSetD3DDefaults; = gx::kBackBufferField |
| 429 | `kD3dDepthStencil` | `0x3b40` | CGxBatch, the argument to the Draw slot | GetDepthStencilSurface, the auto depth buffer; = gx::kDepthSurfaceField |
| 430 | `kD3dResolveSurface` | `0x3b44` | CGxBatch, the argument to the Draw slot | MSAA resolve surface for DeviceReadPixels (0x0068F6A0) |
| 431 | `kD3dEventQuery` | `0x3b48` | CGxBatch, the argument to the Draw slot | IDirect3DQuery9, D3DQUERYTYPE_EVENT |
| 432 | `kD3dPlaceholderTex` | `0x3b58` | CGxBatch, the argument to the Draw slot | 8x8 IDirect3DTexture9 made in ICreateD3dDevice (0x0068F3D0) |
| 436 | `kD3dCursorDirty` | `0x3b4c` | CGxBatch, the argument to the Draw slot | Set to 1 by DeviceSetFormat (0x006904D0), DeviceWM focus-in (0x00690230) and CursorUnlock  |
| 444 | `kCapsBase` | `0x214` | capabilities | --- capabilities ------------------------------------------------------------------------- |
| 445 | `kCapsTextureUnits` | `0x214` | capabilities | MaxSimultaneousTextures, capped at 8 |
| 446 | `kCaps218` | `0x218` | capabilities | D3D writes 0 |
| 447 | `kCaps21C` | `0x21c` | capabilities | D3D writes 1 |
| 448 | `kCapsMaxStreams` | `0x220` | capabilities |  |
| 450 | `kCapsMaxVertexIndex` | `0x230` | capabilities | gx::kGxDeviceBaseVertexMode (0x224) is this block's stream-offset mode. |
| 451 | `kCapsAutoGenMipmaps` | `0x234` | capabilities |  |
| 452 | `kCapsTexFormats` | `0x238` | capabilities | uint32[kTexFormatCount], indexed by EGxTexFormat |
| 453 | `kCapsTex2D` | `0x26c` | capabilities |  |
| 454 | `kCapsTexCube` | `0x270` | capabilities |  |
| 455 | `kCapsTex274` | `0x274` | capabilities | D3D writes 0 |
| 456 | `kCapsTexNpot` | `0x278` | capabilities | 0 only when textures must be powers of two |
| 457 | `kCapsTexNpotConditional` | `0x27c` | capabilities | D3D: NONPOW2CONDITIONAL, forced 0 on NVIDIA |
| 458 | `kCapsMaxTexWidth` | `0x280` | capabilities |  |
| 459 | `kCapsMaxTexHeight` | `0x284` | capabilities |  |
| 460 | `kCapsMaxCubeSize` | `0x288` | capabilities |  |
| 461 | `kCapsMaxTexExtent` | `0x28c` | capabilities |  |
| 462 | `kCapsRtArgb8888` | `0x298` | capabilities |  |
| 463 | `kCapsRtRgb565` | `0x2a4` | capabilities |  |
| 464 | `kCapsRtRg16f` | `0x2b8` | capabilities |  |
| 465 | `kCapsRtR32f` | `0x2bc` | capabilities |  |
| 466 | `kCapsDepthTexD24S8` | `0x2c0` | capabilities |  |
| 468 | `kCapsShaderTarget` | `0x2c8` | capabilities | Per EGxShTarget (0 vertex .. 4 pixel, 6 entries): the supported profile, then the constant |
| 469 | `kCapsShaderConstants` | `0x2e0` | capabilities |  |
| 471 | `kCapsTexFilterTrilinear` | `0x2f8` | capabilities |  |
| 472 | `kCapsTexFilterAniso` | `0x2fc` | capabilities |  |
| 473 | `kCapsMaxAnisotropy` | `0x300` | capabilities |  |
| 474 | `kCapsDepthBias` | `0x304` | capabilities |  |
| 475 | `kCapsColorWrite` | `0x308` | capabilities |  |
| 476 | `kCapsMaxClipPlanes` | `0x30c` | capabilities |  |
| 477 | `kCapsHwCursor` | `0x310` | capabilities |  |
| 478 | `kCapsOcclusionQuery` | `0x314` | capabilities |  |
| 479 | `kCapsPointParameters` | `0x318` | capabilities |  |
| 480 | `kCapsMaxPointSize` | `0x31c` | capabilities | float |
| 481 | `kCapsPointSprites` | `0x320` | capabilities |  |
| 482 | `kCapsBlendFactor` | `0x324` | capabilities |  |
| 483 | `kCaps328` | `0x328` | capabilities | 0x328..0x338: D3D writes 0 |
| 484 | `kCaps338` | `0x338` | capabilities |  |
| 485 | `kCaps344` | `0x344` | capabilities | D3D writes 1 |
| 491 | `kCaps348` | `0x348` | capabilities | ISetCaps' last act, literally `kCaps348 = kCaps34C = (pixelProfile != kPixelProfilePs30)`. |
| 492 | `kCaps34C` | `0x34c` | capabilities | same value as kCaps348 |
| 534 | `kD3dCaps9` | `0x3980` | where the caps block comes from, and what can rewr | The adapter's own D3DCAPS9, copied onto the device object: hardware truth, as opposed to t |
| 535 | `kCaps9VertexShaderVersion` | `0xc4` | where the caps block comes from, and what can rewr |  |
| 536 | `kCaps9MaxVertexShaderConst` | `0xc8` | where the caps block comes from, and what can rewr |  |
| 537 | `kCaps9PixelShaderVersion` | `0xcc` | where the caps block comes from, and what can rewr |  |
| 547 | `kFormatSize` | `0x58` | CGxFormat (kFormatSize bytes) | --- CGxFormat (kFormatSize bytes) -------------------------------------------------------- |
| 548 | `kFmtHwVertexProcessing` | `0x4` | CGxFormat (kFormatSize bytes) | uint8 |
| 549 | `kFmtHwCursor` | `0x5` | CGxFormat (kFormatSize bytes) | uint8 |
| 550 | `kFmtGpuSync` | `0x6` | CGxFormat (kFormatSize bytes) | uint8: sync at present ("fix lag") |
| 551 | `kFmtWindowed` | `0x7` | CGxFormat (kFormatSize bytes) | uint8 |
| 552 | `kFmtKeepAspect` | `0x8` | CGxFormat (kFormatSize bytes) | uint8: the base SetFormat derives kWinAspect from it |
| 553 | `kFmtWindowStyle` | `0xc` | CGxFormat (kFormatSize bytes) | 1 fullscreen window, 2 popup, else overlapped |
| 554 | `kFmtDepthFormat` | `0x10` | CGxFormat (kFormatSize bytes) | EGxFormat |
| 555 | `kFmtWidth` | `0x14` | CGxFormat (kFormatSize bytes) |  |
| 556 | `kFmtHeight` | `0x18` | CGxFormat (kFormatSize bytes) |  |
| 557 | `kFmtBackBuffers` | `0x1c` | CGxFormat (kFormatSize bytes) |  |
| 558 | `kFmtSamples` | `0x20` | CGxFormat (kFormatSize bytes) |  |
| 559 | `kFmtSampleQuality` | `0x24` | CGxFormat (kFormatSize bytes) | float 0..1 |
| 560 | `kFmtColorFormat` | `0x28` | CGxFormat (kFormatSize bytes) | EGxFormat |
| 561 | `kFmtRefreshRate` | `0x2c` | CGxFormat (kFormatSize bytes) |  |
| 562 | `kFmtVSync` | `0x30` | CGxFormat (kFormatSize bytes) | present interval, 0 = immediate |
| 563 | `kFmtStereo` | `0x34` | CGxFormat (kFormatSize bytes) | uint8 |
| 564 | `kFmtMaxVertexProfile` | `0x38` | CGxFormat (kFormatSize bytes) | int, -1 = no cap |
| 565 | `kFmtMaxPixelProfile` | `0x48` | CGxFormat (kFormatSize bytes) | int, -1 = no cap |
| 566 | `kFmtPositionX` | `0x50` | CGxFormat (kFormatSize bytes) |  |
| 567 | `kFmtPositionY` | `0x54` | CGxFormat (kFormatSize bytes) |  |
| 570 | `kWindowed` | `0x1c3` | CGxFormat (kFormatSize bytes) | CGxDevice::IDevIsWindowed reads this byte |
| 607 | `kPoolType` | `0x8` | CGxPool / CGxBuf | 0 vertex, 1 index |
| 608 | `kPoolUsage` | `0xc` | CGxPool / CGxBuf | 0 static, 1 dynamic, 2 stream |
| 609 | `kPoolSize` | `0x10` | CGxPool / CGxBuf | bytes, set by PoolSizeSet |
| 610 | `kPoolApiHandle` | `0x14` | CGxPool / CGxBuf | the backend's object for the pool |
| 611 | `kPoolStreamCursor` | `0x1c` | CGxPool / CGxBuf | stream pools: end of the last buffer placed |
| 612 | `kPoolBufList` | `0x20` | CGxPool / CGxBuf | TSList of the pool's CGxBufs |
| 613 | `kPoolName` | `0x30` | CGxPool / CGxBuf |  |
| 621 | `kBufPool` | `0x8` | CGxPool / CGxBuf | -> CGxPool |
| 623 | `kBufItemCount` | `0x10` | CGxPool / CGxBuf |  |
| 624 | `kBufBytes` | `0x14` | CGxPool / CGxBuf |  |
| 626 | `kBufHasData` | `0x1c` | CGxPool / CGxBuf | uint8: data was written and not discarded |
| 627 | `kBufValid` | `0x1d` | CGxPool / CGxBuf | uint8: the last unlock succeeded |
| 628 | `kBufDirty` | `0x1e` | CGxPool / CGxBuf | uint8, set by the base BufLock/BufData |
| 631 | `kIndexSize` | `0x2` | CGxPool / CGxBuf | Index buffers are 16-bit everywhere: the engine has no 32-bit index path. |
| 634 | `kTexUpdateRect` | `0x0` | CGxTex | CiRect {minY, minX, maxY, maxX}, what to upload next |
| 635 | `kTexWidth` | `0x14` | CGxTex |  |
| 636 | `kTexHeight` | `0x18` | CGxTex |  |
| 637 | `kTexDepth` | `0x1c` | CGxTex |  |
| 638 | `kTexTarget` | `0x20` | CGxTex | EGxTexTarget, 1 = cube map |
| 639 | `kTexFormat` | `0x24` | CGxTex | EGxTexFormat the image is stored as |
| 640 | `kTexDataFormat` | `0x28` | CGxTex | EGxTexFormat the fill callback delivers |
| 641 | `kTexFlags` | `0x2c` | CGxTex | CGxTexFlags |
| 642 | `kTexUserArg` | `0x30` | CGxTex | handed back to the fill callback |
| 643 | `kTexFillCallback` | `0x34` | CGxTex | fills the texels on upload; 0 = cannot update |
| 644 | `kTexApiObject` | `0x38` | CGxTex | D3D: IDirect3DTexture9*, IDirect3DCubeTexture9* for a cube map |
| 645 | `kTexApiObject2` | `0x3c` | CGxTex | always 0 on D3D; "created" is tested as +0x38 // +0x3C |
| 648 | `kTexLinkNext` | `0x44` | CGxTex | The TSLink on the device's texture list (kTexList): the next CGxTex is at +0x44; a value w |
| 649 | `kTexUpdatePending` | `0x5a` | CGxTex | uint8 |
| 650 | `kTexNeedsRecreate` | `0x5b` | CGxTex | uint8 |
| 651 | `kTexFlagsChanged` | `0x5c` | CGxTex | uint8 |
| 685 | `kShaderName` | `0x18` | CGxShader | "Name", or "Name:perm" for one of several permutations |
| 686 | `kShaderRefCount` | `0x1c` | CGxShader |  |
| 687 | `kShaderApiObject` | `0x20` | CGxShader |  |
| 688 | `kShaderTarget` | `0x24` | CGxShader | EGxShTarget |
| 689 | `kShaderValid` | `0x2c` | CGxShader | what CGxShader::Valid returns |
| 690 | `kShaderCreateTried` | `0x30` | CGxShader | CGxShader::Valid calls IShaderCreate while 0 |
| 694 | `kShaderCodeSize` | `0x4c` | CGxShader | bytes in kShaderCode; 0 means the record has none |
| 695 | `kShaderCode` | `0x50` | CGxShader | the compiled bytecode |
| 762 | `kGrowableCount` | `0x4` | base-class functions a backend that is not D3D cal |  |
| 763 | `kGrowableData` | `0x8` | base-class functions a backend that is not D3D cal |  |
| 768 | `kEmergencyLockStride` | `0x14` | base-class functions a backend that is not D3D cal |  |

### `client/warcraftxl/include/wxl/offsets/engine/Liquid.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 57 | `kGeomDirty` | `0x8` |  | non-zero forces a full rebuild |
| 58 | `kGeomChunkBuf` | `0x1c` |  | buffered chunk geometry pointer, null until first built |
| 59 | `kGeomBatchCache` | `0x20` |  | the four descriptor dwords, copied out on the fast path |
| 64 | `kChunkBufVertex` | `0x10` |  | vertex buffer view* |
| 65 | `kChunkBufIndex` | `0x14` |  | index buffer view* |
| 97 | `kSettingsColor0` | `0x300` |  | u32 BGRA |
| 98 | `kSettingsColor1` | `0x304` |  | u32 BGRA |
| 99 | `kSettingsInts` | `0x308` |  | int[4] |
| 100 | `kSettingsFloats` | `0x318` |  | float[18] |
| 116 | `kChunkLvf` | `0x8` |  | liquid vertex format on the chunk (1 = stored u16 UVs) |
| 120 | `kChunkRow0` | `0x34` |  | Grid extent of the emitted lattice, as inclusive bounds. Vertices are emitted rows-outer,  |
| 121 | `kChunkCol0` | `0x38` |  |  |
| 122 | `kChunkRow1` | `0x3c` |  |  |
| 123 | `kChunkCol1` | `0x40` |  |  |
| 151 | `kBufPool` | `0x8` | Gx geometry buffers | Buffer view fields. +0x1C is ours to set once the contents are written; +0x1D is the engin |
| 152 | `kBufStride` | `0xc` | Gx geometry buffers |  |
| 153 | `kBufCount` | `0x10` | Gx geometry buffers |  |
| 154 | `kBufBytes` | `0x14` | Gx geometry buffers |  |
| 155 | `kBufOffset` | `0x18` | Gx geometry buffers |  |
| 156 | `kBufValid` | `0x1c` | Gx geometry buffers |  |
| 157 | `kBufGpuOk` | `0x1d` | Gx geometry buffers |  |
| 159 | `kPoolByteSize` | `0x10` | Gx geometry buffers | / The pool's capacity in bytes, and the ceiling on anything written through a view onto it |
| 192 | `kRowMaxDarkenDepth` | `0x18` | Gx geometry buffers | float, world units |
| 193 | `kRowFogDarkenIntensity` | `0x1c` | Gx geometry buffers | float 0..1 |
| 194 | `kRowAmbDarkenIntensity` | `0x20` | Gx geometry buffers | float 0..1 |
| 195 | `kRowDirDarkenIntensity` | `0x24` | Gx geometry buffers | float 0..1 |
| 205 | `kRowMaterialId` | `0x38` | Gx geometry buffers | u32, valid range {1, 2, 3} |
| 211 | `kDnOceanCloseColor` | `0x10c` | Gx geometry buffers |  |
| 212 | `kDnOceanFarColor` | `0x110` | Gx geometry buffers |  |
| 213 | `kDnRiverCloseColor` | `0x114` | Gx geometry buffers |  |
| 214 | `kDnRiverFarColor` | `0x118` | Gx geometry buffers |  |
| 217 | `kDnOceanAlphaShallow` | `0x140` | Gx geometry buffers | The interpolated shallow/deep alpha pair per family (the light-params water alphas), laid  |
| 218 | `kDnRiverAlphaShallow` | `0x144` | Gx geometry buffers |  |
| 219 | `kDnOceanAlphaDeep` | `0x148` | Gx geometry buffers |  |
| 220 | `kDnRiverAlphaDeep` | `0x14c` | Gx geometry buffers |  |

### `client/warcraftxl/include/wxl/offsets/engine/Lua.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 250 | `kMD5ContextSize` | `0x58` | the point at which a context exists and the interf |  |

### `client/warcraftxl/include/wxl/offsets/engine/Mem.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 56 | `kOffM2SceneCache` | `0x4` | OOM-ladder purge primitives (wxl-engine-reforged) |  |

### `client/warcraftxl/include/wxl/offsets/engine/Shader.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 106 | `kConstCacheBytes` | `0x1000` | the shadow constant caches (CGxDevice::s_shadowCon |  |
| 137 | `kEffectConstFogColorAlphaRef` | `0x0` | the shader-effect constant block (which engine reg | -> pixel c2 |
| 138 | `kEffectConstFogParams` | `0x10` | the shader-effect constant block (which engine reg | -> vertex c30 |
| 174 | `kCollectionVtxSlots` | `0x2c` | collection object layout | vertex permutation slot array (90 entries) |
| 175 | `kCollectionPixSlots` | `0x194` | collection object layout | pixel permutation slot array (16 entries) |
| 204 | `kCgxShaderHandle` | `0x20` | shader wrapper layout (the native per-permutation  | --- shader wrapper layout (the native per-permutation shader object) --------------------- |
| 205 | `kCgxShaderCreated` | `0x30` | shader wrapper layout (the native per-permutation  | non-zero = handle ready, skip re-create at flush |
| 206 | `kCgxShaderByteLen` | `0x4c` | shader wrapper layout (the native per-permutation  |  |
| 207 | `kCgxShaderBytePtr` | `0x50` | shader wrapper layout (the native per-permutation  |  |
| 208 | `kCgxShaderWrapBytes` | `0x60` | shader wrapper layout (the native per-permutation  | allocation size that covers all fields the flush reads |

### `client/warcraftxl/include/wxl/offsets/engine/Sound.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 49 | `kOffGroupMasterVolume` | `0x8` |  |  |

### `client/warcraftxl/include/wxl/offsets/engine/Ui.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 29 | `kUiEnabledFlag` | `0x124c` |  | int field on the root: 1 = UI shown, 0 = UI hidden. Initialized to 1. |

### `client/warcraftxl/include/wxl/offsets/game/ADT.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 59 | `kLiquidTypeMaterialId` | `0x38` | lookups | u32, GetMaterial's usable range is {1, 2, 3} |
| 88 | `kOffChunkBboxPrimary` | `0x4c` | terrain draw-frustum cull |  |
| 89 | `kOffChunkBboxSecondary` | `0x8c` | terrain draw-frustum cull |  |
| 109 | `kTileSlotStride` | `0x4` | tile-slot grid |  |
| 114 | `kOffTileAsyncRead` | `0x70` | tile-area object fields | async-read handle; non-zero while a tile read is in flight |
| 115 | `kOffTileFileBuffer` | `0x80` | tile-area object fields | raw ADT byte buffer; freed by kTileAreaDestroy |
| 116 | `kOffTileFileHandle` | `0x6c` | tile-area object fields | archive file handle of the open tile file (closed by async destroy) |
| 117 | `kOffTileFileSize` | `0x84` | tile-area object fields | byte size of the +0x80 buffer |
| 118 | `kOffTileIdxFirst` | `0x48` | tile-area object fields | first  %d of "<Map>_%d_%d.adt" |
| 119 | `kOffTileIdxSecond` | `0x4c` | tile-area object fields | second %d of "<Map>_%d_%d.adt" |
| 124 | `kOffTileChunkSlots` | `0xbc` | tile-area object fields | The tile's 16x16 built-chunk slots (stride 4): slot (gx & 15) * 16 + (gy & 15), gx / gy be |
| 189 | `kOffAreaLowMinX` | `0x4` | map low-detail (WDL) seam | Allocate low-detail tile: pool-allocates one low-detail tile object (the per-tile low-deta |
| 190 | `kOffAreaLowMinY` | `0x8` | map low-detail (WDL) seam |  |
| 191 | `kOffAreaLowMinZ` | `0xc` | map low-detail (WDL) seam |  |
| 192 | `kOffAreaLowMaxX` | `0x10` | map low-detail (WDL) seam |  |
| 193 | `kOffAreaLowMaxY` | `0x14` | map low-detail (WDL) seam |  |
| 194 | `kOffAreaLowMaxZ` | `0x18` | map low-detail (WDL) seam |  |
| 195 | `kOffAreaLowCenterX` | `0x1c` | map low-detail (WDL) seam |  |
| 196 | `kOffAreaLowCenterY` | `0x20` | map low-detail (WDL) seam |  |
| 197 | `kOffAreaLowCenterZ` | `0x24` | map low-detail (WDL) seam |  |
| 198 | `kOffAreaLowRadius` | `0x28` | map low-detail (WDL) seam |  |
| 199 | `kOffAreaLowOriginX` | `0x2c` | map low-detail (WDL) seam |  |
| 200 | `kOffAreaLowOriginY` | `0x30` | map low-detail (WDL) seam |  |
| 201 | `kOffAreaLowCol` | `0x38` | map low-detail (WDL) seam |  |
| 202 | `kOffAreaLowRow` | `0x3c` | map low-detail (WDL) seam |  |
| 203 | `kOffAreaLowRenderBudget` | `0x40` | map low-detail (WDL) seam | byte budget for render indices, 0xC per unholed cell |
| 204 | `kOffAreaLowMareData` | `0x44` | map low-detail (WDL) seam | -> 545 s16 heightmap (17x17 outer + 16x16 inner) |
| 205 | `kOffAreaLowMahoData` | `0x48` | map low-detail (WDL) seam | -> 16 u16 hole mask, or 0 |
| 213 | `kOffChunkNodeLayerCount` | `0x9` | runtime chunk object fields | draw-node layer count |
| 216 | `kOffChunkMcnkHeader` | `0x110` | runtime chunk object fields ; Chunk object | Chunk object -> MCNK 128-byte data header (= raw MCNK ptr + 8-byte tag). The authoritative |
| 217 | `kOffMcnkNLayers` | `0xc` | runtime chunk object fields |  |
| 221 | `kOffChunkMcly` | `0x12c` | runtime chunk object fields | Raw on-disk MCLY/MCAL base pointers (point into the resident MCNK block, all physical entr |
| 222 | `kOffChunkMcal` | `0x130` | runtime chunk object fields |  |
| 226 | `kOffChunkRawMcnk` | `0x10c` | runtime chunk object fields | raw MCNK (tag+size header) inside the tile buffer |
| 227 | `kOffChunkMcvt` | `0x11c` | runtime chunk object fields | 145 floats, relative heights |
| 228 | `kOffChunkMccv` | `0x120` | runtime chunk object fields | 145 x BGRA vertex colors (vertex format 2 only) |
| 229 | `kOffChunkMcnr` | `0x124` | runtime chunk object fields | 435 signed normal bytes |
| 230 | `kOffChunkMcsh` | `0x128` | runtime chunk object fields | 512-byte shadow bitmap (hdr flags bit0 gates use) |
| 231 | `kOffChunkMcrf` | `0x134` | runtime chunk object fields | u32 refs: doodads first (nDoodadRefs) then wmos |
| 232 | `kOffChunkMclq` | `0x138` | runtime chunk object fields | legacy liquid layers (hdr sizeLiquid > 8 gates) |
| 233 | `kOffChunkMcse` | `0x13c` | runtime chunk object fields | sound emitters (hdr nSndEmitters gates) |
| 238 | `kOffChunkEffectLayerMap` | `0x114` | runtime chunk object fields | The per-cell ground-effect maps the detail-doodad placement (0x007D3390) reads, as pointer |
| 239 | `kOffChunkNoEffectMap` | `0x118` | runtime chunk object fields |  |
| 241 | `kOffChunkDrawBatch` | `0x90` | runtime chunk object fields | Primitive/draw-batch descriptor (the 145-vertex MCVT grid VB/IB) passed to the device Draw |
| 243 | `kOffChunkTexOwnerSrc` | `0x20` | runtime chunk object fields | Source of the tile tex-owner object: (*(chunkObj+0x20) & ~1) + 8. |
| 246 | `kOffChunkLayerRecords` | `0x34` | runtime chunk object fields | Per-layer record array (4 slots, stride 0x14): +0x00 flags, +0x04 diffuse GPU texture hand |
| 247 | `kChunkLayerRecordStride` | `0x14` | runtime chunk object fields |  |
| 269 | `kGxDeviceDrawVtbl` | `0xa8` | terrain surface render (per-chunk draw + multi-pas | Vtable slot of the Draw (DrawIndexedPrimitive) method on the engine::gx::kGxDevicePtr devi |
| 292 | `kOffTexOwnerHandleArray` | `0x60` | terrain surface render (per-chunk draw + multi-pas | Tile tex-owner: per-tile texture-handle array, indexed by MCLY.textureId, stride 8 ([+0] = |
| 293 | `kTexOwnerHandleStride` | `0x8` | terrain surface render (per-chunk draw + multi-pas |  |
| 348 | `kOffChunkNodeFlags` | `0xa` | terrain extra-layer second pass (layers 5..8) | u16: bit0 = mask-family layer, bit2 = cube-env layer |
| 349 | `kOffChunkNodeChunk` | `0x10` | terrain extra-layer second pass (layers 5..8) | chunk object backing the node |
| 350 | `kOffChunkNodeAlphaRT` | `0x84` | terrain extra-layer second pass (layers 5..8) | combined alpha texture handle bound at s(nLayers) |
| 352 | `kOffLayerSlotFlags` | `0x0` | terrain extra-layer second pass (layers 5..8) | u16 MCLY flags low word (anim dir/speed/animate) |
| 353 | `kOffLayerSlotIndex` | `0x2` | terrain extra-layer second pass (layers 5..8) | u16 slot index (0..3) |
| 354 | `kOffLayerSlotTexId` | `0x8` | terrain extra-layer second pass (layers 5..8) | u32 MCLY textureId (dedup key only at draw time) |
| 355 | `kOffLayerSlotAlpha` | `0xc` | terrain extra-layer second pass (layers 5..8) | per-layer alpha handle; 0 on the shader path |
| 356 | `kOffLayerSlotNode` | `0x10` | terrain extra-layer second pass (layers 5..8) | back-pointer to the node |
| 358 | `kOffMapChunkIndexX` | `0x24` | terrain extra-layer second pass (layers 5..8) | local 0..15 (MCIN slot = y*16 + x) |
| 359 | `kOffMapChunkIndexY` | `0x28` | terrain extra-layer second pass (layers 5..8) |  |
| 360 | `kOffMapChunkGlobalX` | `0x34` | terrain extra-layer second pass (layers 5..8) | tileX*16 + localX (0..1023) |
| 361 | `kOffMapChunkGlobalY` | `0x38` | terrain extra-layer second pass (layers 5..8) |  |
| 398 | `kOffLayerSlotTexture` | `0x4` | terrain per-layer UV scale | Resolved-texture pointer inside a layer slot (slot = node + kOffChunkLayerRecords + i*kChu |
| 400 | `kOffTextureName` | `0x6c` | terrain per-layer UV scale | File-name string (NUL-terminated) inside a resolved texture object. |

### `client/warcraftxl/include/wxl/offsets/game/DB2.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 33 | `kRecordSize` | `0x20` |  |  |
| 89 | `kOffModelName` | `0x8` | - | char* model file path |
| 102 | `kOffModelId` | `0x4` | - | u32 CreatureModelData id |
| 119 | `kOffRecordPrefix` | `0x18` | - | char* -> NUL-terminated 2-char race code (e.g. "Hu") |
| 140 | `kOffModel1` | `0x4` | - | char* primary model filename (no path, WITH extension) |
| 141 | `kOffModel2` | `0x8` | - | char* secondary model filename (left/right variant) |
| 142 | `kOffTex1` | `0xc` | - | char* primary texture name |
| 143 | `kOffTex2` | `0x10` | - | char* secondary texture name |
| 144 | `kOffIcon2` | `0x18` | - | char* icon2 string (consumer-defined convention; raw pointer only) |
| 145 | `kOffParticleId` | `0x60` | - | uint32 particle color id |
| 146 | `kRecordSize` | `0x100` | - | byte stride between records in the storage array |

### `client/warcraftxl/include/wxl/offsets/game/Dbc.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 39 | `kSize` | `0x24` | - | ------------------------------------------------------------------------------------------ |
| 41 | `kOffVtable` | `0x0` | - | 8 slots; see the kSlot* indices below |
| 42 | `kOffLoaded` | `0x4` | - | int, 1 once Load has finished |
| 43 | `kOffRecordCount` | `0x8` | - | int, read straight out of the file header |
| 44 | `kOffMaxId` | `0xc` | - | int, -1 before load |
| 45 | `kOffMinId` | `0x10` | - | int, 0x0FFFFFFF before load |
| 46 | `kOffStringTable` | `0x14` | - | char*, the file's string block, allocated whole |
| 51 | `kOffVtableById` | `0x18` | - | standard stores: == vtable - 8 |
| 53 | `kOffRecordBlock` | `0x18` | - | compressible stores: T[] raw, or the RLE blob |
| 56 | `kOffRecords` | `0x1c` | - | standard: T[recordCount], contiguous |
| 57 | `kOffRowPtrs` | `0x1c` | - | compressible: void*[recordCount], by row index |
| 63 | `kOffRecordsById` | `0x20` | - | The by-id table, indexed by (id - minId), zero where no record carries that id. The record |
| 66 | `kSlotDtor` | `0x0` | - | /< scalar deleting destructor |
| 67 | `kSlotLoad` | `0x1` | - | /< Load(allocFile, allocLine) |
| 68 | `kSlotUnused` | `0x2` | - | /< an empty virtual, the same one for every store |
| 69 | `kSlotLoadRecords` | `0x3` | - | /< LoadRecords(file, allocFile, allocLine) |
| 70 | `kSlotGetRecordByIndex` | `0x4` | - | /< GetRecordByIndex(row, out) |
| 71 | `kSlotFreeStorage` | `0x5` | - |  |
| 72 | `kSlotReset` | `0x6` | - |  |
| 73 | `kSlotRecordsById` | `0x7` | - | /< returns &recordsById |
| 172 | `kRecordSize` | `0x2a8` | - | 680 |
| 191 | `kOffName` | `0x220` | - | / The four localized string pointers in the record. Their order follows the file's column  |
| 192 | `kOffRank` | `0x224` | - |  |
| 193 | `kOffDescription` | `0x228` | - |  |
| 194 | `kOffTooltip` | `0x22c` | - |  |

### `client/warcraftxl/include/wxl/offsets/game/Doodad.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 38 | `kMddfPosX` | `0x8` | spawn | The MDDF record kSpawnFromMDDF reads (0x24 bytes, standard MDDF layout, unchanged since Cl |
| 39 | `kMddfPosY` | `0xc` | spawn |  |
| 40 | `kMddfPosZ` | `0x10` | spawn |  |
| 44 | `kMddfScale` | `0x20` | spawn | Uniform scale, u16 with 1024 = 100%. The spawn path's own conversion, at the call site tha |
| 87 | `kRefCount` | `0xa` | placed-doodad def identity (CMapDoodadDef as a TSH | u16; kDoodadPurge frees only at 0 |
| 88 | `kHashKey` | `0x90` | placed-doodad def identity (CMapDoodadDef as a TSH | u32 |
| 89 | `kHashKey2` | `0xa4` | placed-doodad def identity (CMapDoodadDef as a TSH | u32 |
| 96 | `kHashListLinkOffset` | `0x4` | placed-doodad def identity (CMapDoodadDef as a TSH |  |
| 97 | `kHashListFirst` | `0xc` | placed-doodad def identity (CMapDoodadDef as a TSH |  |
| 100 | `kFlags` | `0xc` | placed-doodad object fields | 1 = normal placement |
| 103 | `kPosX` | `0x6c` | placed-doodad object fields | World position (C3Vector). |
| 104 | `kPosY` | `0x70` | placed-doodad object fields |  |
| 105 | `kPosZ` | `0x74` | placed-doodad object fields |  |
| 106 | `kScale` | `0x78` | placed-doodad object fields | uniform scale |
| 111 | `kBBoxMinX` | `0x38` | placed-doodad object fields | bbox min / sphere center / bbox max. WARNING: at spawn all three are set equal to the posi |
| 112 | `kBBoxMinY` | `0x3c` | placed-doodad object fields |  |
| 113 | `kBBoxMinZ` | `0x40` | placed-doodad object fields |  |
| 114 | `kCenterX` | `0x48` | placed-doodad object fields |  |
| 115 | `kCenterY` | `0x4c` | placed-doodad object fields |  |
| 116 | `kCenterZ` | `0x50` | placed-doodad object fields |  |
| 117 | `kBBoxMaxX` | `0x54` | placed-doodad object fields |  |
| 118 | `kBBoxMaxY` | `0x58` | placed-doodad object fields |  |
| 119 | `kBBoxMaxZ` | `0x5c` | placed-doodad object fields |  |
| 124 | `kWorldMatrix` | `0xd8` | placed-doodad object fields | Staging matrix (float[16], row-major) composed once at spawn. NOT what the renderer reads: |
| 125 | `kWorldMatrixTransX` | `0x108` | placed-doodad object fields |  |
| 126 | `kWorldMatrixTransY` | `0x10c` | placed-doodad object fields |  |
| 127 | `kWorldMatrixTransZ` | `0x110` | placed-doodad object fields |  |
| 132 | `kInstance` | `0x34` | model render instance (the object the renderer act ; doodad | doodad -> model render instance (0 mid async-load) |
| 133 | `kInstWorldMatrix` | `0xb4` | model render instance (the object the renderer act | float[16] row-major, READ every frame, never rewritten |
| 134 | `kInstTransX` | `0xe4` | model render instance (the object the renderer act | translation row of the live matrix (= world X/Y/Z) |
| 135 | `kInstTransY` | `0xe8` | model render instance (the object the renderer act |  |
| 136 | `kInstTransZ` | `0xec` | model render instance (the object the renderer act |  |
| 137 | `kInstModel` | `0x2c` | model render instance (the object the renderer act ; instanc | instance -> model cache node |
| 140 | `kModelFullPath` | `0x3c` | model cache node (holds the file path + the parsed | inline NUL-terminated normalized path (take address) |
| 141 | `kModelFileName` | `0x140` | model cache node (holds the file path + the parsed | char* to the bare filename (points into the +0x3C buffer) |
| 142 | `kModelHeader` | `0x150` | model cache node (holds the file path + the parsed | ptr to the parsed MD20 header blob (the local-bounds source) |
| 146 | `kHdrBBoxMinX` | `0xa0` | MD20 header (H = *(model+0x150)): model-LOCAL boun | C3Vector local AABB min |
| 147 | `kHdrBBoxMinY` | `0xa4` | MD20 header (H = *(model+0x150)): model-LOCAL boun |  |
| 148 | `kHdrBBoxMinZ` | `0xa8` | MD20 header (H = *(model+0x150)): model-LOCAL boun |  |
| 149 | `kHdrBBoxMaxX` | `0xac` | MD20 header (H = *(model+0x150)): model-LOCAL boun | C3Vector local AABB max |
| 150 | `kHdrBBoxMaxY` | `0xb0` | MD20 header (H = *(model+0x150)): model-LOCAL boun |  |
| 151 | `kHdrBBoxMaxZ` | `0xb4` | MD20 header (H = *(model+0x150)): model-LOCAL boun |  |
| 154 | `kChunkDoodadLinkOff` | `0xc4` | per-chunk doodad list | --- per-chunk doodad list --- |
| 155 | `kChunkDoodadHead` | `0xcc` | per-chunk doodad list |  |
| 156 | `kNodeDoodad` | `0x4` | per-chunk doodad list |  |

### `client/warcraftxl/include/wxl/offsets/game/GameObject.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 30 | `kTypeHelper` | `0x1a0` | the per-type helper | --- the per-type helper --- The constructor allocates one helper per game-object type (0x3 |
| 31 | `kHelperOwner` | `0x4` | the per-type helper |  |
| 36 | `kHelperWorldDef` | `0x10` | the per-type helper | The world-model helpers (CGGameObject_C::Type_WorldObj and its descendants) keep the def W |
| 42 | `kDescEntry` | `0xc` | descriptor fields (the block unit::kObjectHeaderFi | --- descriptor fields (the block unit::kObjectHeaderField points at, 4 bytes a field) ---  |
| 43 | `kDescScale` | `0x10` | descriptor fields (the block unit::kObjectHeaderFi |  |
| 44 | `kDescDisplayId` | `0x20` | descriptor fields (the block unit::kObjectHeaderFi |  |
| 45 | `kDescFlags` | `0x24` | descriptor fields (the block unit::kObjectHeaderFi |  |
| 46 | `kDescParentRotation` | `0x28` | descriptor fields (the block unit::kObjectHeaderFi |  |
| 47 | `kDescBytes1` | `0x44` | descriptor fields (the block unit::kObjectHeaderFi |  |
| 55 | `kDisplayInfoModel` | `0x4` | GameObjectDisplayInfo.dbc (the client table, loade |  |

### `client/warcraftxl/include/wxl/offsets/game/M2.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 46 | `kOffModelAsyncRead` | `0xc` | load / setup | -> the in-flight async-read object, null once complete |
| 97 | `kOffModelExtSeqCount` | `0x24` | native modern-M2 direct-fill entry points (feature | uint32: sequences whose data streams from .anim |
| 98 | `kOffModelExtSeqArray` | `0x28` | native modern-M2 direct-fill entry points (feature | -> allocator-owned u32 array (count * 4) |
| 99 | `kOffModelExtSeqCursor` | `0x2c` | native modern-M2 direct-fill entry points (feature | uint32: zeroed by kInit's tail |
| 318 | `kOffEmitterCellShift` | `0xc` | particle emitter instance layout | log2(columns) |
| 319 | `kOffEmitterCellWidth` | `0x10` | particle emitter instance layout | 1 / columns |
| 320 | `kOffEmitterCellHeight` | `0x14` | particle emitter instance layout | 1 / rows |
| 321 | `kOffEmitterParticlePool` | `0x34` | particle emitter instance layout | CParticle2[], stride kParticleRecordStride |
| 322 | `kOffEmitterLiveCount` | `0x50` | particle emitter instance layout |  |
| 323 | `kOffEmitterLiveIndices` | `0x54` | particle emitter instance layout | uint32[] into the pool, kOffEmitterLiveCount long |
| 324 | `kOffEmitterRate` | `0x9c` | particle emitter instance layout |  |
| 325 | `kOffEmitterRateVary` | `0xa0` | particle emitter instance layout |  |
| 326 | `kOffEmitterLifespan` | `0xa4` | particle emitter instance layout |  |
| 327 | `kOffEmitterLifespanVary` | `0xa8` | particle emitter instance layout |  |
| 332 | `kOffEmitterBlendState` | `0xd0` | particle emitter instance layout | Material chosen by the model's post-load sizing pass from the record's blend mode and hand |
| 333 | `kOffEmitterMaterialFlags` | `0xd4` | particle emitter instance layout |  |
| 335 | `kOffEmitterHeadCellBlock` | `0xec` | particle emitter instance layout | -> the record's head-cell count+offset pair |
| 336 | `kOffEmitterAtlasRows` | `0x120` | particle emitter instance layout |  |
| 337 | `kOffEmitterAtlasCols` | `0x124` | particle emitter instance layout |  |
| 338 | `kOffEmitterFlags` | `0x134` | particle emitter instance layout |  |
| 339 | `kOffEmitterParentModel` | `0x184` | particle emitter instance layout | frame of reference passed to EmitNewParticles |
| 341 | `kOffParticleAge` | `0x0` | particle emitter instance layout | seconds; the particle dies once it reaches lifespan |
| 342 | `kOffParticlePosition` | `0x4` | particle emitter instance layout |  |
| 343 | `kOffParticleVelocity` | `0x10` | particle emitter instance layout |  |
| 391 | `kParticleRecBlendMode` | `0x28` | particle emitter instance layout |  |
| 392 | `kParticleRecHeadCells` | `0x13c` | particle emitter instance layout | what kOffEmitterHeadCellBlock points at |
| 397 | `kOffEmitterChildCount` | `0x6c` | particle emitter instance layout |  |
| 398 | `kOffEmitterChildArray` | `0x70` | particle emitter instance layout | INLINE array of particle emitter*, not a pointer |
| 426 | `kOffRecordBuffer` | `0x4` | external animation | I/O record field offsets used by the rebase: the buffer base and its byte size. |
| 427 | `kOffRecordSize` | `0x8` | external animation |  |
| 429 | `kOffNodeRecord` | `0x8` | external animation | Load-node field: the I/O record pointer. |
| 439 | `kOffDrawCtxInstance` | `0x60` | per-batch alpha ; draw context | draw context -> render instance |
| 440 | `kOffDrawCtxMaterial` | `0x98` | per-batch alpha ; draw context | draw context -> live material record |
| 442 | `kOffMaterialBlend` | `0x2` | per-batch alpha | Material record: the blend mode (1 = alpha key). |
| 482 | `kShadowRunStride` | `0x3` | bone palette (per-frame skinning matrices; the bon | dwords per co-instance run record |
| 483 | `kShadowRunCountField` | `0x2` | bone palette (per-frame skinning matrices; the bon | dword offset of the "requested count" field |
| 503 | `kOffSceneAnimateHead` | `0x28` | bone palette (per-frame skinning matrices; the bon | on the scene: -> first root M2Instance in the walk |
| 504 | `kOffInstAnimateNext` | `0x44` | bone palette (per-frame skinning matrices; the bon | on M2Instance: -> next root instance in the walk |
| 509 | `kOffSceneAnimateCtx` | `0x84` | bone palette (per-frame skinning matrices; the bon | The per-frame camera-relative context matrix (64-byte 4x4 matrix) the scene's animate prea |
| 590 | `kOffRenderCtxElement` | `0x50` | bone palette (per-frame skinning matrices; the bon | -> current M2Element (see kOffElementAlpha) |
| 591 | `kOffRenderCtxInstance` | `0x60` | bone palette (per-frame skinning matrices; the bon | -> current M2Instance |
| 592 | `kOffElementAlpha` | `0xc` | bone palette (per-frame skinning matrices; the bon | float, consumed by kSetupMaterial's diffuse setup |
| 597 | `kOffElementBatchIndex` | `0x18` | bone palette (per-frame skinning matrices; the bon | The element's own position within its skin's batch array (the draw dispatcher's disabled d |
| 759 | `kOffInstOwnerFlags` | `0x4` | runtime instance object fields | --- runtime instance object fields --- Creation-time "kind" flags, forwarded verbatim from |
| 760 | `kOffInstInitFlags` | `0x10` | runtime instance object fields | init flags (bit 0 = anim init done; bit 6 = char-select present) |
| 761 | `kOffInstModel` | `0x2c` | runtime instance object fields | -> runtime model |
| 762 | `kOffInstScene` | `0x28` | runtime instance object fields | -> the scene this instance belongs to |
| 763 | `kOffInstCmdRingHead` | `0x34` | runtime instance object fields | deferred-command ring cursor; equal to the tail when idle |
| 764 | `kOffInstCmdRingTail` | `0x38` | runtime instance object fields |  |
| 765 | `kOffInstLastAnimFrame` | `0x3c` | runtime instance object fields | uint32: scene frame this instance last animated on |
| 766 | `kOffSceneClock` | `0xc` | runtime instance object fields | uint32 on the scene: absolute animation clock (ms) |
| 767 | `kOffSceneFrame` | `0x14` | runtime instance object fields | uint32 on the scene: the current frame counter |
| 768 | `kOffInstParent` | `0x48` | runtime instance object fields | -> parent M2 instance (null for root) |
| 769 | `kOffInstAttachEnable` | `0x4c` | runtime instance object fields | -> per-attachment enable records (stride 0xC, u8 at +0x8) |
| 770 | `kOffInstAttachSlot` | `0x54` | runtime instance object fields | uint32: attachment index this instance hangs on (0xFFFF = none) |
| 771 | `kOffInstAttachedHead` | `0x58` | runtime instance object fields | -> first attached child instance |
| 772 | `kOffInstAttachedNext` | `0x60` | runtime instance object fields | -> next sibling in the parent's attached-child list |
| 773 | `kOffInstFreezeAnchor` | `0x64` | runtime instance object fields | uint32: nonzero arms externally driven pose freezing |
| 774 | `kOffInstViewDistSq` | `0x88` | runtime instance object fields | float: squared view-space distance (also the draw sort key) |
| 775 | `kOffInstConstTrackGate` | `0x90` | runtime instance object fields | uint32: once-only constant-track sampling gate |
| 776 | `kOffInstBoneStates` | `0x94` | runtime instance object fields | -> runtime bone-state block (stride kRuntimeBoneStride) |
| 779 | `kOffInstSectionVisible` | `0x9c` | runtime instance object fields ; skin | -> uint32[skin->submeshCount], one per submesh, allocated ALL-VISIBLE at initialize. Its o |
| 780 | `kOffInstTexBinding` | `0x174` | runtime instance object fields | shared texture-binding slot mirrored from the parent |
| 781 | `kOffInstSpeedBase` | `0x178` | runtime instance object fields | float: base playback speed |
| 782 | `kOffInstAlphaBase` | `0x17c` | runtime instance object fields | float: base alpha factor |
| 783 | `kOffInstScaleBase` | `0x180` | runtime instance object fields | float[3]: base scale |
| 784 | `kOffInstTransBase` | `0x18c` | runtime instance object fields | float[3]: base translation |
| 785 | `kOffInstSpeedStage` | `0x198` | runtime instance object fields | float: staged speed consumed by children/particles |
| 786 | `kOffInstAlphaStage` | `0x19c` | runtime instance object fields | float: staged alpha |
| 787 | `kOffInstScaleStage` | `0x1a0` | runtime instance object fields | float[3]: staged scale passed into child recursion |
| 788 | `kOffInstTransStage` | `0x1ac` | runtime instance object fields | float[3]: staged translation |
| 797 | `kOffInstBonePalette` | `0x98` | runtime instance object fields | -> bone matrices, row-major 4x4 |
| 798 | `kBonePaletteStride` | `0x40` | runtime instance object fields | one bone matrix |
| 800 | `kOffInstPlacement` | `0xb4` | runtime instance object fields ; Model | Model->world placement, an INLINE 64-byte matrix (the shadow loop reads &instance+0xb4). |
| 804 | `kOffInstViewRoot` | `0xf4` | runtime instance object fields | Instance root, an INLINE 64-byte matrix written by the model's threaded animate entry (0x0 |
| 808 | `kOffInstRenderObjArray` | `0x2bc` | runtime instance object fields | -> void*[] (one pointer per skin batch) |
| 815 | `kOffInstLightingCallbackFn` | `0x2ac` | runtime instance object fields | Per-instance lighting-callback pair: +0x2AC a function pointer, +0x2B0 its userData. Wired |
| 816 | `kOffInstLightingUserData` | `0x2b0` | runtime instance object fields |  |
| 825 | `kOffInstSectionOverride` | `0x2d0` | runtime instance object fields ; notably the boneInfluences  | -> per-instance batch/section override arrays ([0] = batches, [2] = sections). When non-nu |
| 828 | `kOffRenderObjFlags` | `0x160` | runtime instance object fields | Visibility flags written by the visibility pass (0x97f570) into each render object. Bit 2  |
| 829 | `kOffInstShared` | `0x2c` | runtime instance object fields | -> the shared model data this instance draws |
| 833 | `kOffInstEmitterCells` | `0x2c0` | runtime instance object fields | stride kEmitterCellStride |
| 834 | `kOffInstEmitterArray` | `0x2c4` | runtime instance object fields | -> particle emitter*[] |
| 835 | `kEmitterCellStride` | `0x88` | runtime instance object fields |  |
| 836 | `kOffCellLifespan` | `0x44` | runtime instance object fields |  |
| 837 | `kOffCellRate` | `0x50` | runtime instance object fields |  |
| 838 | `kOffCellEnabled` | `0x80` | runtime instance object fields |  |
| 841 | `kOffModelFlags` | `0x8` | runtime model object fields | bit 2 selects the sibling-file open flag |
| 842 | `kOffModelPathStem` | `0x3c` | runtime model object fields | model path stem (no extension) |
| 843 | `kOffModelHeader` | `0x150` | runtime model object fields ; parsed in place | -> raw .m2 file buffer (parsed in place -> becomes the header) |
| 844 | `kOffModelFileSize` | `0x16c` | runtime model object fields | byte size of the .m2 file buffer at +0x150 |
| 845 | `kOffModelSkin` | `0x170` | runtime model object fields | -> live parsed skin profile (valid at/after skin finalize) |
| 846 | `kOffModelSubMeshCopy` | `0x188` | runtime model object fields | -> per-submesh object array ptr (written by kFinalizeSkin; not freed on re-call) |
| 847 | `kOffModelSubmeshBuf` | `0x18c` | runtime model object fields | -> submesh copy buffer ptr (written by kFinalizeSkin; not freed on re-call) |
| 853 | `kOffSharedCoInstanceCount` | `0x190` | runtime model object fields ; skin | uint32: how many CO-INSTANCE copies of the whole vertex array the shared vertex buffer hol |
| 863 | `kOffSharedAnimGateCount` | `0x198` | runtime model object fields ; instance | uint16: count of bones carrying flags & 0x2F8 (0x200 "transformed" + the billboard bits 0x |
| 871 | `kOffSharedMaxInstances` | `0x194` | runtime model object fields ; skin | uint32: MAX INSTANCES per batched draw, computed by kFinalizeSkin as min(65536 / skin->ind |
| 874 | `kOffHdrGlobalFlags` | `0x10` | parsed file-header fields | bit 0x20 = model carries physics |
| 878 | `kOffHdrSeqCount` | `0x1c` | parsed file-header fields | sequence records |
| 879 | `kOffHdrSeqPtr` | `0x20` | parsed file-header fields | -> sequence records (stride kSeqStride) |
| 880 | `kSeqStride` | `0x40` | parsed file-header fields |  |
| 881 | `kOffSeqFlags` | `0xc` | parsed file-header fields | bit 0x1 = plays once to its end, then holds |
| 891 | `kOffSeqId` | `0x0` | parsed file-header fields | uint16: the value SetBoneSequence/lookup functions match against |
| 892 | `kOffSeqSubId` | `0x2` | parsed file-header fields | uint16 |
| 893 | `kOffSeqLength` | `0x4` | parsed file-header fields | uint32, milliseconds |
| 894 | `kOffSeqMovingSpeed` | `0x8` | parsed file-header fields | float |
| 895 | `kOffSeqFrequency` | `0x10` | parsed file-header fields | int16 |
| 896 | `kOffSeqRangeMin` | `0x14` | parsed file-header fields | uint32 |
| 897 | `kOffSeqRangeMax` | `0x18` | parsed file-header fields | uint32 |
| 898 | `kOffSeqBlendTime` | `0x1c` | parsed file-header fields | uint32 |
| 899 | `kOffSeqBBoxMin` | `0x20` | parsed file-header fields | float[3] |
| 900 | `kOffSeqBBoxMax` | `0x2c` | parsed file-header fields | float[3] |
| 901 | `kOffSeqBBoxRadius` | `0x38` | parsed file-header fields | float |
| 902 | `kOffSeqNextAnimation` | `0x3c` | parsed file-header fields | int16: variation-chain index (0xFFFF/-1 = none) |
| 903 | `kOffSeqAliasNext` | `0x3e` | parsed file-header fields | uint16: alias-chain target when kOffSeqFlags bit 0x40 is set |
| 904 | `kOffHdrBoneCount` | `0x2c` | parsed file-header fields |  |
| 905 | `kOffHdrBoneArray` | `0x30` | parsed file-header fields | -> bone records (post-fixup data ptr) |
| 913 | `kOffHdrKeyBoneCount` | `0x34` | parsed file-header fields | keyBoneLookup: the table SetBoneSequence/GetBoneSequenceId's own `slot` param resolves thr |
| 914 | `kOffHdrKeyBoneArray` | `0x38` | parsed file-header fields | -> int16 array, -1 = this model has no such key bone |
| 915 | `kOffHdrAttachCount` | `0xf0` | parsed file-header fields | attachment records |
| 916 | `kOffHdrAttachPtr` | `0xf4` | parsed file-header fields | -> attachment records (stride kAttachStride) |
| 917 | `kAttachStride` | `0x28` | parsed file-header fields |  |
| 918 | `kOffAttachBone` | `0x4` | parsed file-header fields | uint16: palette slot a child attached here rides |
| 919 | `kOffAttachPos` | `0x8` | parsed file-header fields | float[3]: offset applied to that slot |
| 922 | `kOffHdrBoneCombosCount` | `0x78` | parsed file-header fields | boneCombos M2Array {count, offset}: the per-section bone-index window the palette upload w |
| 923 | `kOffHdrBoneCombosPtr` | `0x7c` | parsed file-header fields | -> uint16 array |
| 924 | `kOffHdrBoneIdxLutCount` | `0xf8` | parsed file-header fields | count of bone-index-by-id LUT entries |
| 925 | `kOffHdrBoneIdxLutPtr` | `0xfc` | parsed file-header fields | -> bone-index-by-id LUT (uint16 array, indexed by key_bone_id) |
| 928 | `kBoneStride` | `0x58` | bone record fields (in the header bone array) | --- bone record fields (in the header bone array) --- |
| 929 | `kOffBoneKeyId` | `0x0` | bone record fields (in the header bone array) | key_bone_id: canonical slot id (negative = none) |
| 930 | `kOffBoneFlags` | `0x4` | bone record fields (in the header bone array) | bone flags |
| 931 | `kOffBoneParent` | `0x8` | bone record fields (in the header bone array) | int16 parent index (0xFFFF = root) |
| 932 | `kOffBoneNameCrc` | `0xc` | bone record fields (in the header bone array) | CRC32 of the bone name string (for name-based remap) |
| 933 | `kOffBoneTransTrack` | `0x10` | bone record fields (in the header bone array) | translation track head (kOffTrackTimestampsCount = outer slots) |
| 934 | `kOffBoneRotTrack` | `0x24` | bone record fields (in the header bone array) | rotation track head |
| 935 | `kOffBoneScaleTrack` | `0x38` | bone record fields (in the header bone array) | scale track head |
| 936 | `kOffBonePivot` | `0x4c` | bone record fields (in the header bone array) | pivot (bone origin in bind space) |
| 946 | `kRuntimeBoneStride` | `0xac` | runtime bone-state block (per bone, stride kRuntim | --- runtime bone-state block (per bone, stride kRuntimeBoneStride) --- The persisted sampl |
| 947 | `kOffRtBoneTransValue` | `0x8` | runtime bone-state block (per bone, stride kRuntim | float[3]: last sampled translation |
| 948 | `kOffRtBoneQuatValue` | `0x1c` | runtime bone-state block (per bone, stride kRuntim | float[4]: last sampled rotation quaternion |
| 949 | `kOffRtBoneScaleValue` | `0x34` | runtime bone-state block (per bone, stride kRuntim | float[3]: last sampled scale |
| 950 | `kOffRtBoneAssignedSeq` | `0x48` | runtime bone-state block (per bone, stride kRuntim | uint16: assigned sequence (0xFFFF = inherit) |
| 951 | `kOffRtBoneSeqStart` | `0x4c` | runtime bone-state block (per bone, stride kRuntim | int32: sequence start anchor (absolute clock ms) |
| 952 | `kOffRtBoneSeqEnd` | `0x50` | runtime bone-state block (per bone, stride kRuntim | int32: sequence end (absolute clock ms) |
| 953 | `kOffRtBoneTimeScale` | `0x54` | runtime bone-state block (per bone, stride kRuntim | float: playback speed factor |
| 954 | `kOffRtBoneTimeOffset` | `0x5c` | runtime bone-state block (per bone, stride kRuntim | int32: offset into the sequence |
| 955 | `kOffRtBoneBlendSeq` | `0x6c` | runtime bone-state block (per bone, stride kRuntim | uint16: blend-from sequence (0xFFFF = no blend) |
| 956 | `kOffRtBoneProcMatrix` | `0x88` | runtime bone-state block (per bone, stride kRuntim | -> externally driven per-frame matrix (null = none) |
| 957 | `kOffRtBoneFlagMask` | `0x8c` | runtime bone-state block (per bone, stride kRuntim | uint32: runtime bone-flag override mask |
| 958 | `kOffRtBonePendingSeq` | `0x90` | runtime bone-state block (per bone, stride kRuntim | uint32: pending sequence bookkeeping (0xFFFFFFFF idle) |
| 961 | `kOffRtBonePrevSeqId` | `0x94` | runtime bone-state block (per bone, stride kRuntim | uint16: SetBoneSequence's own prevSeqId param, as last written |
| 962 | `kOffRtBoneBlendWeight` | `0xa8` | runtime bone-state block (per bone, stride kRuntim | float: current blend weight (0 = none) |
| 965 | `kOffCmoRace` | `0x18` | CharModelObject fields | uint32 race id |
| 966 | `kOffCmoGender` | `0x1c` | CharModelObject fields | uint32 gender (0 = male, 1 = female) |
| 967 | `kOffCmoSceneNode` | `0x38` | CharModelObject fields | -> SceneNode (the root scene node for this character) |
| 974 | `kOffCmoSlotItemId` | `0x428` | CharModelObject fields | Per-internal-model-slot cached item display id (uint32[0xC], slots 0..0xB), written by Cha |
| 977 | `kOffSceneNodeOwner` | `0x28` | SceneNode fields | -> CharModelObject that owns this scene node |
| 980 | `kOffTrackTimestampsCount` | `0x4` | track object fields read by the evaluators | --- track object fields read by the evaluators --- |
| 981 | `kOffTrackTimestampsPtr` | `0x8` | track object fields read by the evaluators |  |
| 982 | `kOffTrackValuesCount` | `0xc` | track object fields read by the evaluators |  |
| 983 | `kOffTrackValuesPtr` | `0x10` | track object fields read by the evaluators |  |
| 984 | `kTrackTimestampStride` | `0x4` | track object fields read by the evaluators | one timestamp = u32 ms |
| 985 | `kTrackVec3Stride` | `0xc` | track object fields read by the evaluators | one vec3 value = 3 floats |
| 986 | `kTrackQuatStride` | `0x8` | track object fields read by the evaluators | one compressed quat = 4 int16 |
| 988 | `kOffRuntimeBoneAnimIdx` | `0x44` | track object fields read by the evaluators | Runtime-bone field: the current animation index used to pick the per-animation inner slot. |
| 991 | `kOffRibbonLayerCount` | `0x118` | ribbon-emitter object fields | draw-loop bound |
| 992 | `kOffRibbonTexHandlePtr` | `0x12c` | ribbon-emitter object fields | -> per-layer texture-handle array (stride 4) |
| 1468 | `kOffCharComponentInstance` | `0x38` | per-frame / slot hook signatures | / -> the model instance the geoset decision writes to. Reloaded from the component before  |
| 1472 | `kOffCharComponentGeosetSlots` | `0x144` | per-frame / slot hook signatures | / uint32[19]: the chosen geoset id per customization slot, filled by the CCharacterCompone |
| 1553 | `kOffSourceDescMode` | `0x5` | character skin composition: where the sheet's sour | / Byte 5 of the six-byte description both copies receive, which is kOffTexEntryAlphaBits s |
| 1632 | `kOffTexEntryPendingRead` | `0x18` | character skin composition: where the sheet's sour | / The read in flight. Note that the image pointer below is allocated BEFORE the bytes arri |
| 1633 | `kOffTexEntryWidth` | `0x1c` | character skin composition: where the sheet's sour | uint16, height at +0x1E |
| 1634 | `kOffTexEntryLevelCount` | `0x20` | character skin composition: where the sheet's sour | byte |
| 1635 | `kOffTexEntryAlphaBits` | `0x21` | character skin composition: where the sheet's sour | byte: 0, 1, 4 or 8 |
| 1636 | `kOffTexEntryImage` | `0xac` | character skin composition: where the sheet's sour | the container file, verbatim; null until the |
| 1638 | `kOffTexEntryFlags` | `0xb0` | character skin composition: where the sheet's sour | async load has landed |
| 1677 | `kCharRegionRectStride` | `0x10` | character skin composition: where the sheet's sour |  |
| 1686 | `kOffSectionTexturePaths` | `0x10` | character skin composition: where the sheet's sour | char*[3], one per composition slot |
| 1687 | `kOffSectionFlags` | `0x1c` | character skin composition: where the sheet's sour |  |
| 1691 | `kOffCharComponentRace` | `0x18` | character skin composition: where the sheet's sour | / Component fields the load above writes. |
| 1692 | `kOffCharComponentSex` | `0x1c` | character skin composition: where the sheet's sour |  |
| 1698 | `kOffCharComponentDirty` | `0xc` | character skin composition: where the sheet's sour | / Bitmask, one bit per region: the regions the composition still owes a repaint. The secti |
| 1706 | `kOffCharComponentRebuild` | `0x8` | character skin composition: where the sheet's sour | / int[]: the loaded texture handle per (slot + sectionType * kSectionSlotCount). / The com |
| 1719 | `kOffCharComponentSheet` | `0x190` | character skin composition: where the sheet's sour |  |
| 1720 | `kOffCharComponentTextures` | `0x194` | character skin composition: where the sheet's sour |  |
| 1725 | `kOffCharComponentComposeRequest` | `0x52c` | character skin composition: where the sheet's sour | / The component REQUEST, which is what the sheet texture is ultimately fed from: its sourc |
| 1972 | `kOffSharedIndexPool` | `0x178` | character skin composition: where the sheet's sour | -> GxPool backing the shared index buffer |
| 1973 | `kOffSharedIndexBuf` | `0x17c` | character skin composition: where the sheet's sour | -> the shared index GxBuf (built/valid at +0x1C/+0x1D) |
| 1978 | `kOffSharedInstanceCopies` | `0x190` | character skin composition: where the sheet's sour | / uint32: how many copies of the model one shared index buffer holds, for the batched-dood |
| 1982 | `kOffSharedFileRecord` | `0x4` | character skin composition: where the sheet's sour | / -> the shared file record, whose flags dword at kOffM2FileFlags picks how both fills spe |
| 1983 | `kOffM2FileFlags` | `0x4` | character skin composition: where the sheet's sour |  |
| 1992 | `kOffInstGeometryCtx` | `0x2d0` | character skin composition: where the sheet's sour | / Per-instance geometry context: the compacted draw list plus the buffers it feeds. |
| 1993 | `kOffGeoCtxGroups` | `0x8` | character skin composition: where the sheet's sour | -> group records, stride kGeoCtxGroupStride |
| 1994 | `kOffGeoCtxGroupCount` | `0xc` | character skin composition: where the sheet's sour | uint32 |
| 1995 | `kOffGeoCtxRanges` | `0x10` | character skin composition: where the sheet's sour | -> {firstBatch, lastBatch} pairs, stride 8 |
| 1996 | `kOffGeoCtxIndexBuf` | `0x18` | character skin composition: where the sheet's sour | -> the index GxBuf this fills |
| 1997 | `kGeoCtxGroupStride` | `0x30` | character skin composition: where the sheet's sour | a group's first dword indexes kOffGeoCtxRanges |
| 1999 | `kOffGxBufBuilt` | `0x1c` | character skin composition: where the sheet's sour | / The index buffer's two "already built" flags; both set means the contents still stand. |
| 2000 | `kOffGxBufValid` | `0x1d` | character skin composition: where the sheet's sour |  |
| 2002 | `kGxVtblBufLock` | `0xd8` | character skin composition: where the sheet's sour | / Device vtable slots the assembly rides: lock returns the writable buffer, unlock commits |
| 2003 | `kGxVtblBufUnlock` | `0xdc` | character skin composition: where the sheet's sour |  |
| 2252 | `kOffSharedVertexPool` | `0x180` | character skin composition: where the sheet's sour | / The vertex GxBuf the fill writes into, and the pool backing it. Both null until the firs |
| 2253 | `kOffSharedVertexBuf` | `0x184` | character skin composition: where the sheet's sour |  |
| 2255 | `kModelVertexStride` | `0x30` | character skin composition: where the sheet's sour | / The model vertex record, as kReadVertices lays it down and as the programmable fill copi |
| 2256 | `kOffVertexPosition` | `0x0` | character skin composition: where the sheet's sour | float[3] |
| 2257 | `kOffVertexWeights` | `0xc` | character skin composition: where the sheet's sour | uint8[4], one per slot, summing to 255 |
| 2258 | `kOffVertexBoneSlots` | `0x10` | character skin composition: where the sheet's sour | uint8[4] palette slots; the one field the fill rewrites |

### `client/warcraftxl/include/wxl/offsets/game/Unit.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 60 | `kUnitModelField` | `0xb4` | object field offsets ; unit object | unit object -> body model |
| 61 | `kModelParentField` | `0x48` | object field offsets ; model | model -> parent model (0 = root) |
| 62 | `kUnitPositionField` | `0x798` | object field offsets ; unit object | unit object -> world position (3 floats x, y, z) |
| 63 | `kObjectHeaderField` | `0x8` | object field offsets ; any object | any object -> header carrying its GUID and type mask |
| 64 | `kHeaderGuidField` | `0x0` | object field offsets ; header | header -> GUID |
| 65 | `kHeaderTypeField` | `0x8` | object field offsets ; header | header -> type mask; what kGetObjectByGuid filters on |
| 70 | `kUnitCharacterComponentField` | `0xb4c` | object field offsets ; unit object | unit object -> owned CharacterComponent (the equip/model render state CharEquipSlotUpdate  |
| 77 | `kDescUnitBoundingRadius` | `0x104` | unit descriptor fields (the block kObjectHeaderFie | --- unit descriptor fields (the block kObjectHeaderField points at: 4 bytes a field) --- U |
| 78 | `kDescUnitCombatReach` | `0x108` | unit descriptor fields (the block kObjectHeaderFie |  |
| 79 | `kDescUnitMountDisplayId` | `0x114` | unit descriptor fields (the block kObjectHeaderFie |  |
| 84 | `kVtNamePosition` | `0x8` | virtual slots shared by every object type | anchor above the model, where the client hangs the name |
| 85 | `kVtPosition` | `0xb` | virtual slots shared by every object type | world position; the base implementation reports the origin |
| 86 | `kVtRawPosition` | `0xc` | virtual slots shared by every object type |  |
| 89 | `kVtFacing` | `0xd` | virtual slots shared by every object type | Orientation, radians counter-clockwise from +X: facing 0 points along +X, cos is x and sin |

### `client/warcraftxl/include/wxl/offsets/game/WMO.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 31 | `kOffRootAsyncHandle` | `0x1dc` | load (rewrite buffers before the native parse) |  |
| 39 | `kGroupWalkCursorOffset` | `0x58` | load (rewrite buffers before the native parse) | 0x14-byte outer chunk header + 0x44-byte MOGP body prefix: the fixed offset kGroupParse al |
| 46 | `kGroupBufFlags` | `0x1c` | load (rewrite buffers before the native parse) ; u32 | u32   -> kOffGroupFlags |
| 47 | `kGroupBufBbox` | `0x20` | load (rewrite buffers before the native parse) ; 6 x f | 6 x f -> kOffGroupBbox |
| 48 | `kGroupBufTransBatches` | `0x3c` | load (rewrite buffers before the native parse) ; u16 | u16   -> kOffGroupTransBatchCount |
| 76 | `kOffGroupAsyncHandle` | `0x194` | the actual chunk walkers (the code a native modern |  |
| 110 | `kMomtStride` | `0x40` | MOMT material record (stride 0x40, based at kOffMa | --- MOMT material record (stride 0x40, based at kOffMaterialBase, INSIDE the root file buf |
| 111 | `kOffMomtFlags` | `0x0` | MOMT material record (stride 0x40, based at kOffMa | u32; read at material+0 by both batch draws (0x007A9380 / |
| 122 | `kOffMomtShader` | `0x4` | MOMT material record (stride 0x40, based at kOffMa | u32; CreateMaterial rewrites 3/5/6 -> 4 when tex2 is empty |
| 127 | `kOffMomtBlend` | `0x8` | MOMT material record (stride 0x40, based at kOffMa | u32 |
| 128 | `kOffMomtTexture1` | `0xc` | MOMT material record (stride 0x40, based at kOffMa | u32 MOTX offset / modern FileDataID |
| 129 | `kOffMomtTexture2` | `0x18` | MOMT material record (stride 0x40, based at kOffMa | u32 MOTX offset / modern FileDataID |
| 130 | `kOffMomtDiffColor` | `0x1c` | MOMT material record (stride 0x40, based at kOffMa | u32 BGRA diffuse tint colour |
| 131 | `kOffMomtHandle1` | `0x38` | MOMT material record (stride 0x40, based at kOffMa | runtime texture handle (0 = not loaded yet) |
| 132 | `kOffMomtHandle2` | `0x3c` | MOMT material record (stride 0x40, based at kOffMa | runtime texture handle |
| 140 | `kOffMomtLayerEnv` | `0x2c` | MOMT material record (stride 0x40, based at kOffMa |  |
| 148 | `kMobaStride` | `0x18` | MOBA render batch (stride 0x18, based at group+0x0 | --- MOBA render batch (stride 0x18, based at group+0x0F8, INSIDE the group file buffer) -- |
| 149 | `kOffMobaBbox` | `0x0` | MOBA render batch (stride 0x18, based at group+0x0 | 6 x i16 (min xyz, max xyz), group-local |
| 150 | `kOffMobaStartIndex` | `0xc` | MOBA render batch (stride 0x18, based at group+0x0 | u32 into MOVI |
| 151 | `kOffMobaCount` | `0x10` | MOBA render batch (stride 0x18, based at group+0x0 | u16 index count |
| 152 | `kOffMobaMinIndex` | `0x12` | MOBA render batch (stride 0x18, based at group+0x0 | u16 first vertex index |
| 153 | `kOffMobaMaxIndex` | `0x14` | MOBA render batch (stride 0x18, based at group+0x0 | u16 last vertex index |
| 154 | `kOffMobaFlags` | `0x16` | MOBA render batch (stride 0x18, based at group+0x0 | u8; the client uses the HIGH nibble as frame scratch |
| 155 | `kOffMobaMaterial` | `0x17` | MOBA render batch (stride 0x18, based at group+0x0 | u8 material index -- read at NINE sites, six in render |
| 158 | `kOffMobaMaterialModern` | `0xa` | MOBA render batch (stride 0x18, based at group+0x0 | Modern delta: the material index moved to a u16 here, and the flag bit that announces it s |
| 184 | `kOffGroupFormatFlags` | `0x198` | MOBA render batch (stride 0x18, based at group+0x0 | Group two-UV format flag. Set once at group finalize (0x007D8561 `or [group+0x198],8`) iff |
| 188 | `kOffMohdFlags` | `0x3c` | MOHD (root+kOffMohd content) | u32; bit 0x1 / 0x4 / 0x8 gate group-side behaviour |
| 203 | `kOffModfScale` | `0x3e` | instance placement (per-instance scale) | MODF record: u16 per-instance scale at +0x3E (factor = value/1024; 0 and 1024 both mean 1. |
| 210 | `kOffInstanceRenderMatrix` | `0x70` | instance placement (per-instance scale) | Instance transform matrices (4x4 row-major floats, translation in row 3): +0x70 the model- |
| 211 | `kOffInstanceCollisionMatrix` | `0xb0` | instance placement (per-instance scale) |  |
| 295 | `kOffGroupMorb` | `0xfc` | material / visibility guards | / MORB stores a content pointer with no count (the stock walker derives nothing for it). |
| 298 | `kOffMohd` | `0x120` | root object fields | MOHD content pointer (see kOffMohdFlags) |
| 299 | `kOffRootBuffer` | `0x1cc` | root object fields | root buffer pointer |
| 300 | `kOffRootSize` | `0x1d0` | root object fields | root buffer byte size |
| 301 | `kOffNameInline` | `0x1c` | root object fields | inline path string |
| 302 | `kOffMaterialBase` | `0x160` | root object fields | material-record base pointer |
| 303 | `kOffMaterialCount` | `0x19c` | root object fields | material count |
| 304 | `kOffGroupArray` | `0x1f8` | root object fields | group runtime-object array (stride 4) |
| 305 | `kOffGroupCount` | `0x1f4` | root object fields | group count (the group-array bound) |
| 307 | `kOffMogiTable` | `0x130` | root object fields | group-info table base pointer |
| 308 | `kOffMogiCount` | `0x16c` | root object fields | group-info entry count (kRootSlots[4].countField) |
| 309 | `kMogiStride` | `0x20` | root object fields | group-info entry stride |
| 310 | `kOffMogiBbox` | `0x4` | root object fields | bbox min within an entry (max at +0x10) |
| 313 | `kOffMosb` | `0x12c` | root object fields | MOSB content pointer (skybox name; empty = no skybox) |
| 314 | `kOffMopt` | `0x138` | root object fields | MOPT content pointer (portal planes) |
| 315 | `kOffMoptCount` | `0x174` | root object fields | MOPT entry count |
| 316 | `kOffModn` | `0x150` | root object fields | MODN content pointer (doodad name blob) |
| 317 | `kOffRootModd` | `0x154` | root object fields | MODD content pointer (doodad placements) |
| 320 | `kOffGroupBuffer` | `0x184` | group object fields | group buffer pointer |
| 321 | `kOffGroupSize` | `0x188` | group object fields | group buffer byte size |
| 322 | `kOffGroupRoot` | `0x18c` | group object fields | -> parent root object |
| 324 | `kOffGroupMotv` | `0xf0` | group object fields | MOTV content pointer (base UV set) |
| 325 | `kOffGroupMonr` | `0xec` | group object fields | MONR content pointer (vertex normals) |
| 326 | `kOffGroupVertexCount` | `0x15c` | group object fields | MOVT entry count (kGroupSlots[2].countField) |
| 328 | `kOffGroupBbox` | `0x34` | group object fields | Group bbox (min xyz, max xyz), passed whole to kBspInit; kOffGroupBboxMinZ below is its 3r |
| 335 | `kOffGroupLiquidType` | `0x144` | group object fields | Resolved LiquidType.dbc id, filled during group finalize: MOGP+0x48 verbatim for a modern- |
| 347 | `kOffInstanceRoot` | `0xf4` | visibility-probe entries and globals (cull path) | Instance field: pointer to the owning root object (the one with the inline path at kOffNam |
| 350 | `kOffInstanceDoodadSet` | `0x100` | visibility-probe entries and globals (cull path) | u32 selected set index |
| 351 | `kOffInstanceExtraSets` | `0x150` | visibility-probe entries and globals (cull path) | u16[3] extra set indices (0 = unused) |
| 356 | `kOffInstanceHashKey` | `0x24` | visibility-probe entries and globals (cull path) | The instance's hash key in the map-obj-def hash (kMapObjDefHash): the MODF uniqueId for an |
| 358 | `kOffInstanceBbox` | `0x48` | visibility-probe entries and globals (cull path) | World AABB of the placement (min xyz, max xyz), written at spawn from the MODF extents. |
| 362 | `kOffRootLoaded` | `0x1e0` | visibility-probe entries and globals (cull path) | Root field: nonzero once the root is fully loaded (the explicit spawn waits on kWaitLoad w |
| 364 | `kOffRootMods` | `0x14c` | visibility-probe entries and globals (cull path) | -> MODS array (SMODoodadSet, stride kModsStride) |
| 365 | `kOffRootDoodadSets` | `0x188` | visibility-probe entries and globals (cull path) | u32 doodad-set count (MODS_size / 0x20) |
| 366 | `kOffRootDoodadDefs` | `0x190` | visibility-probe entries and globals (cull path) | u32 doodad-def count (MODD bound) |
| 367 | `kModsStride` | `0x20` | visibility-probe entries and globals (cull path) | SMODoodadSet stride |
| 368 | `kOffModsStart` | `0x14` | visibility-probe entries and globals (cull path) | u32 first MODD index in the set |
| 369 | `kOffModsCount` | `0x18` | visibility-probe entries and globals (cull path) | u32 MODD count in the set |
| 403 | `kOffOccluderVertices` | `0x4` | visibility-probe entries and globals (cull path) |  |
| 415 | `kClipBufferSlots` | `0x180` | visibility-probe entries and globals (cull path) |  |
| 420 | `kOffGroupBboxMinZ` | `0x3c` | visibility-probe entries and globals (cull path) | / Group bbox minimum Z (MOGP header +0x14), i.e. the floor of the antiportal slab. |
| 422 | `kOffGroupIntBatchCount` | `0x5e` | visibility-probe entries and globals (cull path) | / Batch counts CreateOccluders zeroes so the antiportal shell itself never renders. |
| 423 | `kOffGroupExtBatchCount` | `0x60` | visibility-probe entries and globals (cull path) |  |
| 445 | `kOffGroupFlags` | `0x30` | group fields the outdoor rule reads | MOGP flags, copied by kGroupParse |
| 446 | `kOffGroupTransBatchCount` | `0x5c` | group fields the outdoor rule reads | u16, MOGP+0x28 |
| 459 | `kOffGroupBsp` | `0x64` | camera-in-group containment (cull path) | BSP container (null when the group has no BSP) |
| 460 | `kOffGroupMovi` | `0xe0` | camera-in-group containment (cull path) | triangle vertex indices (u16[3] per face, stride 6) |
| 461 | `kOffGroupMovt` | `0xe8` | camera-in-group containment (cull path) | vertices (C3Vector, stride 0x0C) |
| 465 | `kOffGroupMoviCount` | `0x154` | camera-in-group containment (cull path) | MOVI index count (MOVI_size / 2) |
| 466 | `kOffGroupFaceCount` | `0x150` | camera-in-group containment (cull path) | MOPY face count (MOPY_size / 2) |
| 468 | `kOffBspNodes` | `0x0` | camera-in-group containment (cull path) | node array pointer (0 when the group has no BSP) |
| 469 | `kOffBspMobr` | `0x8` | camera-in-group containment (cull path) | collision face-index array (u16 into MOVI) |
| 470 | `kOffBspMobrCount` | `0x10` | camera-in-group containment (cull path) | collision face-index count |
| 471 | `kOffBspBboxMax` | `0x58` | camera-in-group containment (cull path) | local bbox max (3 floats); min at +0x4C |

### `client/warcraftxl/include/wxl/offsets/game/Weather.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 32 | `kIntensity` | `0x8` |  | Current storm intensity. The engine's intensity setter does not snap this: it walks the va |
| 39 | `kRainCount` | `0x13c` |  | Non-zero counts of live precipitation of each kind; the engine's type query reports 1/2/3  |
| 40 | `kSnowCount` | `0x140` |  |  |
| 41 | `kSandCount` | `0x144` |  |  |

### `client/warcraftxl/include/wxl/offsets/game/World.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 74 | `kOffTileAsyncRead` | `0x70` | terrain re-stream (force a reload of the loaded AD | Tile fields: async read-in-flight handle (0 = idle), open file handle, raw ADT file-buffer |
| 75 | `kOffTileFileHandle` | `0x6c` | terrain re-stream (force a reload of the loaded AD |  |
| 76 | `kOffTileFileBuffer` | `0x80` | terrain re-stream (force a reload of the loaded AD |  |
| 77 | `kOffTileFileId` | `0x84` | terrain re-stream (force a reload of the loaded AD |  |
| 81 | `kOffTileIdxFirst` | `0x48` | terrain re-stream (force a reload of the loaded AD |  |
| 82 | `kOffTileIdxSecond` | `0x4c` | terrain re-stream (force a reload of the loaded AD |  |
| 172 | `kWorldFrameCamera` | `0x7e20` | cursor world pick | Active camera within a world frame. CGWorldFrame::GetActiveCamera (0x004F5960) returns it, |
| 174 | `kWorldFrameStandInBytes` | `0x10000` | cursor world pick | Fields the world render path touches reach 0xB18; a stand-in frame reserves well past that |
| 185 | `kWorldFrameInput` | `0xa0` | cursor world pick | CGWorldFrame::SetupDefaultAction refreshes its hit-test point from the active input object |
| 186 | `kInputCursorNdcX` | `0x1224` | cursor world pick |  |
| 187 | `kInputCursorNdcY` | `0x1228` | cursor world pick |  |
| 223 | `kWorldFrameCursorDdcX` | `0x310` | cursor world pick | Scratch hit-test coordinates populated by the world frame's default-action setup. Do not u |
| 224 | `kWorldFrameCursorDdcY` | `0x314` | cursor world pick |  |
| 259 | `kOffAsyncFile` | `0x0` | async I/O queue primitives | Async-read record field offsets, as the tile-area load routine 0x007D7150 and the drain us |
| 260 | `kOffAsyncBuffer` | `0x4` | async I/O queue primitives |  |
| 261 | `kOffAsyncSize` | `0x8` | async I/O queue primitives |  |
| 262 | `kOffAsyncCtx` | `0xc` | async I/O queue primitives |  |
| 263 | `kOffAsyncCallback` | `0x10` | async I/O queue primitives |  |

### `client/warcraftxl/include/wxl/offsets/game/WorldScene.hpp`

| line | name | value | section / owner hint | comment |
|---|---|---|---|---|
| 39 | `kOffGroupBboxMin` | `0x0` |  | wmo::kAddOccluderEdge, __fastcall(groupEntry): a one-line wrapper stamping a WMO group's b |
| 40 | `kOffGroupBboxMax` | `0xc` |  |  |
| 52 | `kOffGroupLinkPtr` | `0x20` | groupEntry | groupEntry -> owning map-object instance ("defInstance") two-level link. groupEntry+0x20 i |
| 53 | `kOffGroupIndex` | `0x50` |  | int, index into the shared model's group table |
| 54 | `kOffLinkDefInst` | `0x8` |  | linkNode + 0x8 = defInstance (map-object instance*) |
