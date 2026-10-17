# WarcraftXL core

**The WarcraftXL runtime for the stock `Wow.exe` 3.3.5a (build 12340).**

A mod is written once against the [SDK](../sdk/) and runs wherever a runtime loads it. This is
the runtime for the shipped client: `WarcraftXL.dll`, a 32-bit DLL that the Windows loader maps
into `Wow.exe` through an import the patcher adds, and that stands up the SDK's mod host inside
the game.

This is the first slice. It proves the path from a double-click on the client to a mod's first
log line: injection, discovery, manifest, version negotiation, the C ABI. It raises no events,
publishes no services and detours nothing in the client. Hooks are the next slice.

## Layout

```
CMakeLists.txt             WarcraftXL.dll + wxl-core-patcher.exe, the SDK pulled in once
src/
  common/Log.*             the log: one file per session, every line written through at once
  runtime/Main.cpp         DllMain: opens the log, starts the core thread
  runtime/Core.*           the core thread: paths, the log sink, the mod host
  patcher/Main.cpp         the patcher's command line
  patcher/PeImage.*        the PE toolbox: sections, imports, large-address-aware
  patcher/PatchScript.*    the registry of byte-edit scripts the patcher runs (empty for now)
include/wxl/offsets/       the generated client offset tables (see generated/README.md)
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
later configure fail. Both binaries land in `build/Release/`:

| File | What |
|---|---|
| `WarcraftXL.dll` | the runtime; links the static CRT, depends on `kernel32.dll` only |
| `wxl-core-patcher.exe` | adds the `WarcraftXL.dll!WarcraftXL` import to a client executable |

MSBuild's file tracker fails with `FTK1011` when the build path is very long; if that happens,
configure the build tree somewhere short.

## Installing

Everything goes next to the client executable. Nothing is installed anywhere else.

### 1. The DLL

Copy `build/Release/WarcraftXL.dll` into the client folder, beside `Wow.exe`, over the legacy
framework's `WarcraftXL.dll` if one is there. Same file name, same export, same import: the
client cannot tell the two apart, so this is a replacement, not a second install.

**If your `Wow.exe` is already patched for WarcraftXL, that is the whole install.** The legacy
patcher's `.wxl` section imports `WarcraftXL.dll!WarcraftXL`, which is exactly what this DLL
exports; running the patcher on such an image reports it as already patched and changes nothing
but the large-address-aware flag. In this workspace `client/game/base/Wow.exe` is such an image.

### 2. Patching a stock executable

The separate case: a client that was never patched (`Wow.orig.exe` here is the stock image). The
patcher takes a 32-bit PE, appends a section named `.wxl` that carries one import,
`WarcraftXL.dll!WarcraftXL`, and sets the large-address-aware flag so the client gets the full
4 GB of address space. The import is what makes the Windows loader map the DLL before the
client's first instruction runs; `WarcraftXL` is an empty exported function that exists only to be
imported.

Patch **to a new file**, leaving the original as it is:

```
wxl-core-patcher.exe Wow.orig.exe Wow-core.exe
```

Or **in place**, which first copies the original to `<file>.orig`:

```
wxl-core-patcher.exe Wow.exe
```

Running the patcher on an already patched image is a no-op that says so. The client does not
care about its executable's name, so a differently named patched copy is a normal way to keep the
stock executable and the patched one side by side.

### 3. A mod

The runtime loads every mod folder under `Mods\` beside the executable. The SDK's example mod is
the one to start with; it logs one line when it loads, which is the whole test of this slice.

```sh
cd ../sdk/template
cmake -S . -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
```

That produces `build/dist/wxl.example/` with `mod.json` and `bin/x86/example.dll` (and an x64 DLL
this runtime ignores). Copy the folder as it is:

```
<client>\
  Wow-core.exe
  WarcraftXL.dll
  Mods\
    wxl.example\
      mod.json
      bin\x86\example.dll
```

A mod built from the template links the dynamic CRT of the Visual Studio that built it, so the
x86 VC++ Redistributable of that version must be installed on the machine that runs the game. A
machine with Visual Studio has it.

## Running, and where the log is

Launch the patched executable. The log is **`Logs\WarcraftXL.log` beside the executable**, created
fresh on every start (the `Logs\` folder is created if missing). It does not depend on the working
directory. Every line is written to the OS as it is produced: a crash on the very next instruction
loses nothing. The legacy framework wrote `Logs\wxl-core.log`; a file of that name is never this
runtime's.

With the example mod in place, the log reads like this:

```
[17:41:37.276] [INFO ] [core] WarcraftXL.dll starting (build Oct 17 2026 17:40:48), loader thread 41180
[17:41:37.276] [INFO ] [core] core thread 42132 up
[17:41:37.276] [INFO ] [core] runtime core (WarcraftXL core) version 100, abi 0.1, target win-x86, mods in D:\...\Mods
[17:41:37.276] [INFO ] [host] scanning D:\...\Mods
[17:41:37.637] [INFO ] [example] loaded on WarcraftXL core
[17:41:37.637] [INFO ] [host] loaded wxl.example 1.0.0 (Example) from bin/x86/example.dll
[17:41:37.637] [INFO ] [host] 1 mod(s) loaded, 0 not
[17:41:37.637] [INFO ] [core] mod wxl.example 1.0.0 (Example) is up
[17:41:37.637] [INFO ] [core] core ready: 1 mod(s) loaded
[17:41:48.057] [INFO ] [core] WarcraftXL.dll detaching
```

`[example] loaded on WarcraftXL core` is the mod's own line, through the host's `log` call, with
the runtime name it was handed in `wxl_runtime_info`. A mod the host refused is reported with its
reason on a `[host] refused ...` line and again in the `[core] mod ... was not loaded:` summary.

The line format is `[HH:MM:SS.mmm] [LEVEL] [tag] message`. `WXL_LOG_LEVEL` in the environment sets
the threshold (`trace`, `debug`, `info`, `warn`, `error`, `off`, or `0`-`5`); the default is
`info`. `debug` adds the host's stage lines. When a debugger is attached every line also goes to
`OutputDebugString`.

## What this slice does and does not do

- **Loading happens on the core's own thread, not the client's main thread.** `DllMain` only opens
  the log and starts a thread; the thread runs once the process is past its loader lock and loads
  the mods there, while the client boots in parallel. `mod.h` says `wxl_mod_load` is called on the
  main thread before `WXL_STAGE_EARLY`; moving it there needs a seam into the client's own boot,
  which is a hook, which is the next slice.
- **Only `WXL_STAGE_EARLY` is reported**, right after loading: by definition nothing the runtime
  offers is up yet. Storage, graphics, script, ready and shutdown are client milestones the core
  cannot see without hooks.
- **Nothing is unloaded.** There is no point before process exit at which the core could run the
  shutdown stage on a live client, so the host lives until the process ends and mods are never
  told to go. A mod's `wxl_mod_unload` is not called in this slice.
- **No events, no services.** A mod that subscribes gets `WXL_SUBSCRIPTION_NONE`; a mod whose
  manifest requires a service is refused by the host with the service's name.
- **No guard around mod code.** A mod that faults in `wxl_mod_load` takes the client down with it.
- The patcher's `PatchScript` registry is wired but empty: the legacy framework's own scripts
  (glue unlock, named patch archives) belong to features this slice does not have.

## When it fails

**The client says `WarcraftXL.dll was not found`** -- the DLL is not beside the patched
executable. The loader resolves the import before anything runs; there is no log yet at that
point.

**`0xc000007b` at start** -- a 64-bit `WarcraftXL.dll` next to the 32-bit client. Configure with
`-A Win32`.

**No `Logs\WarcraftXL.log` at all** -- the DLL did not load, or the folder beside the executable is
not writable. Check the first case with `dumpbin -imports Wow.exe | findstr WarcraftXL`.

**`[host] refused wxl.example: ...`** -- the reason follows: an `abi` the runtime does not accept,
a binary the manifest promises at a path that is not there, a required service the runtime does
not publish. `[host] skipped ...` means the manifest has no `win-x86` entry, which is an ordinary
outcome.

**`[host] refused ...: The specified module could not be found`** -- the mod DLL exists but one
of *its* dependencies does not, usually the VC++ Redistributable noted above.

**`LNK1104: cannot open file 'WarcraftXL.dll'`** -- the DLL is loaded by a running client. Close
it.

## License

GPLv3. The PE toolbox and the patcher's command line are carried over from
[`client/warcraftxl`](../../../client/warcraftxl/), GPLv3 as well, with their copyright headers.
The SDK this links is MIT.
