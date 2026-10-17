# WarcraftXL core

**The WarcraftXL runtime for the stock `Wow.exe` 3.3.5a (build 12340).**

A mod is written once against the [SDK](../sdk/) and runs wherever a runtime loads it. This is
the runtime for the shipped client: `WarcraftXL.dll`, a 32-bit DLL that the Windows loader maps
into `Wow.exe` through an import the patcher adds, and that stands up the SDK's mod host inside
the game.

Two slices so far. The first proved the path from a double-click on the client to a mod's first
log line: injection, discovery, manifest, version negotiation, the C ABI. The second, this one,
moves mod loading onto the client's main thread before the client's first instruction, adds the
hook engine behind `wxl.hook` / `wxl.hook.raw`, adds the `wxl.patch` service, raises the first
real event (`WXL_EV_WorldEnter`) from a detour, and turns the legacy patcher's two byte scripts
into mods.

## Layout

```
CMakeLists.txt             WarcraftXL.dll + wxl-core-patcher.exe + the mods, the SDK pulled in once
deps/minhook/              MinHook 1.3.3 (BSD-2-Clause, its own LICENSE.txt), built as a static lib
src/
  common/Log.*             the log: one file per session, every line written through at once
  runtime/Main.cpp         the two exports (WarcraftXL, WarcraftXLEntry) and DllMain
  runtime/Core.*           Start(): the hook engine, the services, the seams, the mod host
  runtime/Image.*          is this process Wow.exe build 12340 (PE timestamp)
  runtime/WorldSeam.*      the detour on World::Preload that raises WXL_EV_WorldEnter
  hook/Engine.*            detour chains over MinHook: attach, detach, priority
  hook/Points.def          the named hook points (name, address via site.hpp, signature text)
  services/HookService.*   wxl.hook and wxl.hook.raw
  services/PatchService.*  wxl.patch: checked, journaled byte writes; a read-only-data search
  patcher/Main.cpp         the patcher's command line
  patcher/PeImage.*        the PE toolbox: sections, imports, entry point, code caves
  patcher/PatchScript.*    the registry of byte-edit scripts the patcher runs (empty: see mods/)
mods/
  wxl.glue-unlock/         bypasses the Lua/XML signature check (6 edits through wxl.patch)
  wxl.named-archives/      widens patch-?.MPQ to patch-*.MPQ (2 edits through wxl.patch)
include/wxl/offsets/       the generated client offset tables and site.hpp (see generated/README.md)
tools/                     the generators behind include/wxl/offsets/
docs/                      audits and notes
```

The mod host itself -- discovery, manifests, load order, admission, binary loading, the service
registry, the event bus, the stages -- is the SDK's `host/` library, built here from the checkout
and linked into the DLL. Nothing of it is duplicated.

## Building

Requirements: CMake 3.21+, Visual Studio 2022 or newer with the *Desktop development with C++*
workload (the x86 compiler comes with the *MSVC v14x C++ x64/x86 build tools* component), and a
checkout of `WarcraftXL/sdk` next to this folder (or elsewhere: `-DWXL_SDK_DIR=<path>`).

```sh
cmake -S . -B build -G "Visual Studio 18 2026" -A Win32
cmake --build build --config Release
```

`-A Win32` is required: the client is a 32-bit process and the configure stops otherwise. Any
installed Visual Studio generator works in place of `18 2026`. Always configure into `build/`
(gitignored) or a folder outside the repo: a `CMakeCache.txt` left in the source root makes every
later configure fail. Everything lands in `build/Release/`:

| Path | What |
|---|---|
| `WarcraftXL.dll` | the runtime; links the static CRT, depends on `kernel32.dll` only |
| `wxl-core-patcher.exe` | adds the imports and the entry trampoline to a client executable |
| `Mods/wxl.glue-unlock/` | the mod folder, ready to copy: `mod.json` + `bin/x86/glue-unlock.dll` |
| `Mods/wxl.named-archives/` | likewise |

`-DWXL_CORE_MODS=OFF` leaves the mods out. Each mod is also an ordinary SDK mod and builds on its
own from its folder (`cmake -S mods/wxl.glue-unlock -B <tree> -G "Visual Studio 18 2026"`), where
`wxl_add_mod` produces `<tree>/dist/<id>/`. Both are `win-x86` only: they edit addresses in
`Wow.exe` build 12340 and declare `wxl.patch` as required, so any other runtime refuses them before
they load.

MSBuild's file tracker fails with `FTK1011` when the build path is very long; if that happens,
configure the build tree somewhere short.

## Installing

Everything goes next to the client executable. Nothing is installed anywhere else.

### 1. The DLL

Copy `build/Release/WarcraftXL.dll` into the client folder, beside `Wow.exe`, over the legacy
framework's `WarcraftXL.dll` if one is there. Same file name, same anchor export, same import: the
client cannot tell the two apart, so this is a replacement, not a second install.

### 2. The executable

The patcher takes a 32-bit PE and, depending on what it finds:

- **A stock executable** (`Wow.orig.exe` here): appends a section `.wxl` carrying two imports,
  `WarcraftXL.dll!WarcraftXL` (the anchor that makes the loader map the DLL) and
  `WarcraftXL.dll!WarcraftXLEntry`, then a section `.wxle` holding the entry trampoline, and
  repoints the entry point at it. The trampoline calls `WarcraftXLEntry` through its import slot
  and jumps to the original entry point. It holds no absolute address, so it works whatever base
  the image is loaded at.
- **An executable patched by the first slice's patcher, or by the legacy one** (`.wxl` present,
  `.wxle` absent; `client/game/base/Wow.exe` in this workspace is such an image): adds the entry
  import in a section `.wxli` and the trampoline in `.wxle`. The legacy patcher's static byte edits,
  if any, are left as they are.
- **An executable with both** (`.wxl` and `.wxle`): a no-op that says so.

In every case the large-address-aware flag is set so the client gets the full 4 GB.

Patch **to a new file**, leaving the original as it is:

```
wxl-core-patcher.exe Wow.orig.exe Wow-core.exe
```

Or **in place**, which first copies the original to `<file>.orig` (an existing `.orig` is kept):

```
wxl-core-patcher.exe Wow.exe
```

**Without the trampoline the runtime still works**, the old way: `DllMain` sees no `.wxle` and
starts a core thread that loads the mods while the client boots. The log says so plainly
(`the executable was patched by an older patcher ...`), `wxl_mod_load` is then not on the main
thread, and `WXL_STAGE_EARLY` runs late. Run the patcher on the executable to fix it.

### 3. Mods

The runtime loads every mod folder under `Mods\` beside the executable. The two shipped mods go in
as built:

```
<client>\
  Wow-core.exe
  WarcraftXL.dll
  Mods\
    wxl.glue-unlock\
      mod.json
      bin\x86\glue-unlock.dll
    wxl.named-archives\
      mod.json
      bin\x86\named-archives.dll
```

Both check the bytes they are about to change. On a client that already carries the same edits
statically (the legacy patcher applied both) they find the patched bytes, log `already present`
and change nothing. On a stock client they apply them at the entry point, before the client reads
a single interface file or scans for an archive. On anything else they refuse, log what they found,
undo what they had done and are not loaded.

A mod built from the SDK template links the dynamic CRT of the Visual Studio that built it, so the
x86 VC++ Redistributable of that version must be installed on the machine that runs the game. The
two mods built from this tree link the static CRT and need nothing.

## Running, and where the log is

Launch the patched executable. The log is **`Logs\WarcraftXL.log` beside the executable**, created
fresh on every start (the `Logs\` folder is created if missing). It does not depend on the working
directory. Every line is written to the OS as it is produced: a crash on the very next instruction
loses nothing. The legacy framework wrote `Logs\wxl-core.log`; a file of that name is never this
runtime's.

With the trampoline in place and the two mods installed on a stock client, the log should read
like this (composed from the test harness run and the lines the code writes at those sites; not
yet captured from the game itself):

```
[18:22:21.635] [INFO ] [core] WarcraftXL.dll starting (build ...), loader thread 23388
[18:22:21.635] [INFO ] [core] entry trampoline present; mods load at the entry point
[18:22:21.640] [INFO ] [core] entry trampoline on thread 23388, before the client's entry point
[18:22:21.640] [INFO ] [core] runtime core (WarcraftXL core) version 200, abi 0.1, target win-x86, mods in ...\Mods
[18:22:21.640] [INFO ] [core] service wxl.hook version 1
[18:22:21.640] [INFO ] [core] service wxl.hook.raw version 1
[18:22:21.640] [INFO ] [core] service wxl.patch version 1
[18:22:21.646] [INFO ] [hook] attached 'core.WorldEnter' @0x00781500 (priority 0, 1 on this address)
[18:22:21.646] [INFO ] [host] scanning ...\Mods
[18:22:22.068] [INFO ] [patch] applied 'DeleteInterfaceFiles+2F' @0x005F4DBF (1 byte(s))
...
[18:22:22.068] [INFO ] [glue-unlock] signature check bypassed: 6 edit(s) applied, 0 already present
[18:22:22.068] [INFO ] [host] loaded wxl.glue-unlock 1.0.0 (Glue unlock) from bin/x86/glue-unlock.dll
[18:22:22.100] [INFO ] [patch] applied 'patch-%s-?.MPQ' @0x009E2709 (1 byte(s))
[18:22:22.100] [INFO ] [patch] applied 'patch-?.MPQ' @0x009E2716 (1 byte(s))
[18:22:22.100] [INFO ] [named-archives] patch archive scan widened: 2 literal(s) edited, 0 already present
[18:22:22.100] [INFO ] [host] loaded wxl.named-archives 1.0.0 (Named archives) from bin/x86/named-archives.dll
[18:22:22.534] [INFO ] [host] 2 mod(s) loaded, 0 not
[18:22:22.534] [INFO ] [core] core ready: 2 mod(s) loaded
```

The thread id on the `entry trampoline on thread` line is the loader thread's: the client's main
thread. Every `[patch] applied` line names the patch, the address and the size; a refused write is
an `[ERROR] [patch] ... refused, found [..], expected [..]` line and nothing is written.

The line format is `[HH:MM:SS.mmm] [LEVEL] [tag] message`. `WXL_LOG_LEVEL` in the environment sets
the threshold (`trace`, `debug`, `info`, `warn`, `error`, `off`, or `0`-`5`); the default is
`info`. `debug` adds the host's stage lines and the world-enter line. When a debugger is attached
every line also goes to `OutputDebugString`.

## What the runtime offers a mod

**Services**, published before any mod loads, so a manifest's `requires` is answered:

| Service | Version | What |
|---|---|---|
| `wxl.hook` | 1 | detours by name, over the points in `src/hook/Points.def` (`World.Preload`, `Map.Load`) |
| `wxl.hook.raw` | 1 | detours by address; `image_base`, `image_build` (12340, or 0 outside the client) |
| `wxl.patch` | 1 | checked, journaled byte writes; `restore`; `matches`; `find` in read-only data |

`wxl.patch` is declared in the SDK as `include/wxl/services/patch.h`, with the same gating as
`wxl.hook.raw`: a runtime compiled from source does not publish it, and a mod that needs it says
so in its manifest.

Detours **chain**: several parties may attach to one address; each one's `original` is the next
party, the last one's is the engine function; `priority` orders them (lower runs first, equal keeps
attach order) and `detach` closes the chain over one link while the others keep working. MinHook
holds one detour per target, so it is pointed at a six-byte stub per address, `jmp [slot]`, and the
slot holds the current head: attaching or detaching any link, the head included, is a pointer
store, and nothing is re-patched on a live address. Outside `Wow.exe` build 12340 (recognised by
its PE timestamp) the named points are not published and `WorldEnter` is not raised, since their
addresses mean nothing there; `wxl.hook.raw` and `wxl.patch` still work on whatever the process is.

**Events**: `WXL_EV_WorldEnter`, raised after `World::Preload` returns from a call with the loading
screen (the login and teleport path; the preloads around a cinematic camera, a far-sight target or
a transport leg do not raise it), with the map id out of the client's `s_mapId` and the position
the world was loaded around. Every other event is still unsubscribable (`WXL_SUBSCRIPTION_NONE`).

## What this slice does and does not do

- **Mods load on the client's main thread, before its first instruction**, under the entry
  trampoline. `wxl_mod_load` and `WXL_STAGE_EARLY` run there, synchronously. The loader lock is
  not held at that point, which is what makes `LoadLibrary` of a mod safe.
- **Only `WXL_STAGE_EARLY` is reported.** Storage, graphics, script, ready and shutdown are client
  milestones that need their own seams.
- **Nothing is unloaded.** There is no point before process exit at which the core could run the
  shutdown stage on a live client, so the host lives until the process ends. `wxl_mod_unload` is
  not called, and the patch journal is not replayed.
- **No guard around mod code.** A mod that faults in `wxl_mod_load` takes the client down with it.
- `World::Preload` is the one detour the core installs. It is tagged `manual` (verified) in the
  decompiled fork; the choice is explained in `src/hook/Points.def`.
- The patcher's `PatchScript` registry is wired and empty: the byte edits the legacy patcher
  carried are mods now, applied at run time with their expected bytes checked.

## When it fails

**The client says `WarcraftXL.dll was not found`** -- the DLL is not beside the patched
executable. The loader resolves the import before anything runs; there is no log yet at that
point.

**`0xc000007b` at start** -- a 64-bit `WarcraftXL.dll` next to the 32-bit client. Configure with
`-A Win32`.

**No `Logs\WarcraftXL.log` at all** -- the DLL did not load, or the folder beside the executable is
not writable. Check the first case with `dumpbin -imports Wow.exe | findstr WarcraftXL`.

**`[WARN ] [core] the executable was patched by an older patcher`** -- no entry trampoline; mods
load late, on a thread. Run `wxl-core-patcher.exe` on the executable.

**`[ERROR] [patch] '...' refused, found [..], expected [..]`** -- the bytes at that address are not
what the mod expected: another tool changed them, or the executable is not build 12340. Nothing
was written. The mod that asked undoes its earlier edits and is not loaded.

**`[host] refused wxl.glue-unlock: requires service wxl.patch`** -- the runtime that loaded it is
not this one. The two mods are for the stock client only.

**`[host] refused ...: The specified module could not be found`** -- the mod DLL exists but one
of *its* dependencies does not, usually the VC++ Redistributable noted above.

**`LNK1104: cannot open file 'WarcraftXL.dll'`** -- the DLL is loaded by a running client. Close
it.

## License

GPLv3. The PE toolbox, the patcher's command line, the hook engine and the two mods' byte sets are
carried over from [`client/warcraftxl`](../../../client/warcraftxl/), GPLv3 as well, with their
copyright headers. The glue-unlock byte set is originally from a third-party extension pack (MIT,
(c) Alyst3r). MinHook in `deps/minhook` is BSD-2-Clause, (c) Tsuda Kageyu, unchanged. The SDK this
links is MIT.
