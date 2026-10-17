<div align="center">

# WarcraftXL core

**The runtime that loads WarcraftXL mods into World of Warcraft 3.3.5a**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Client](https://img.shields.io/badge/client-3.3.5a%20(12340)-orange.svg)](#-installing)
[![ABI](https://img.shields.io/badge/ABI-0.1-blue.svg)](https://github.com/WarcraftXL/sdk)
[![Targets](https://img.shields.io/badge/targets-x86%20%7C%20x64-lightgrey.svg)](#-overview)

</div>

---

> [!IMPORTANT]
> The 32-bit build replaces the DLL of any earlier WarcraftXL framework installed beside the
> client: the file name, the export and the patch section are the same, so only one can be present.
> Move the old one aside before copying this one over.

## 🚀 Overview

One codebase, built twice, so a mod written against the [SDK](https://github.com/WarcraftXL/sdk)
runs on both the client you own and the client we compile.

| | `bin/x86/WarcraftXL.dll` | `bin/x64/WarcraftXL.dll` |
|---|---|---|
| loads into | the stock `Wow.exe` 3.3.5a, build 12340 | [`WarcraftXL/client`](https://github.com/WarcraftXL/client), the recompiled client |
| gets in by | an import and an entry trampoline the patcher adds | the client calling `WarcraftXLInit` |
| attaches to the game by | a detour at a resolved address | the client's own source calling in |

Above that line the two are the same logic: the mod host, manifests and admission, the event bus,
the lifecycle, the log.

### Key features

- **Loads before the client does.** The patcher points the executable's entry at a 19-byte
  trampoline, so mods load and `WXL_STAGE_EARLY` runs on the main thread before the client's first
  instruction — early enough to change what the client reads while starting.
- **Chained detours.** Several mods may hook the same function; `original` walks the chain and any
  link can detach, including the first, while calls are in flight.
- **Byte patches that refuse to be wrong.** `wxl.patch` takes the bytes it expects to find and
  declines, with a log line, when memory says otherwise. Every write is journalled and reversible.
- **Addresses that fail at compile time.** Nothing is hardcoded: sites resolve by name against
  tables generated from the decompiled client, so a renamed function breaks the build instead of
  the game.
- **An unbuffered log.** One write per line, so a crash keeps everything up to it.

## 📋 Table of contents

- [Overview](#-overview)
- [Building](#-building)
- [Installing](#-installing)
- [What this runtime publishes](#-what-this-runtime-publishes)
- [The mods it ships](#-the-mods-it-ships)
- [Layout](#-layout)
- [Troubleshooting](#-troubleshooting)
- [License](#-license)

## ⚡ Building

### Prerequisites

- Visual Studio 2022 or newer, with the x86 **and** x64 toolsets
- CMake 3.21 or newer
- A checkout of [`WarcraftXL/sdk`](https://github.com/WarcraftXL/sdk) beside this one, or
  `-DWXL_SDK_DIR=<path>`

### Build both architectures

```sh
cmake -S . -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
```

No `-A`: the top level is a driver that configures the real build once per architecture and
collects the result in `build/dist/`:

```
build/dist/
    bin/x86/WarcraftXL.dll
    bin/x86/wxl-core-patcher.exe
    bin/x64/WarcraftXL.dll
    Mods/wxl.glue-unlock/{mod.json,bin/x86/glue-unlock.dll}
    Mods/wxl.named-archives/{mod.json,bin/x86/named-archives.dll}
```

### Build one

```sh
cmake -S . -B build/x86 -G "Visual Studio 18 2026" -A Win32 -DWXL_CORE_ARCH=win-x86
```

`WXL_CORE_TARGETS` selects which architectures the driver builds; `WXL_CORE_DIST` moves the
collection directory.

## 📦 Installing

### Into the stock client (32-bit)

1. Close the game.
2. Copy `build/dist/bin/x86/WarcraftXL.dll` next to `Wow.exe`.
3. Patch the executable, once:
   ```sh
   wxl-core-patcher.exe Wow.exe
   ```
   It writes `Wow.exe.orig` beside it and is idempotent: run it again and it says so, byte for
   byte. Pass a second path to write elsewhere and leave the input alone.
4. Copy the mods you want into `Mods/`, each as its own folder with a `mod.json` and
   `bin/x86/<name>.dll`.
5. Launch. The log is `Logs/WarcraftXL.log`, beside the executable.

A client already patched by an earlier WarcraftXL only needs the trampoline added; the patcher
detects that and says `upgraded`. Without the trampoline the runtime still works, on its own
thread, and says so in the log.

### Into the recompiled client (64-bit)

The client links `WarcraftXL.lib` and calls `WarcraftXLInit` from
[`include/wxl/core/init.h`](include/wxl/core/init.h). Mods are read from `Mods/` beside the
executable, with `bin/x64/` binaries.

## 🔌 What this runtime publishes

What a mod can reach **today**, by build:

| | x86 | x64 |
|---|---|---|
| `wxl.hook` (named points) | yes | not yet |
| `wxl.hook.raw` (raw addresses) | yes | never: no fixed addresses in a program we compile |
| `wxl.patch` (byte patches) | yes | never: nothing to patch in a program we compile |
| `wxl.object`, `wxl.script`, `wxl.storage`, `wxl.net`, `wxl.ui` | not yet | not yet |
| events raised | `WorldEnter` | none yet |
| lifecycle stages | `EARLY` | `EARLY` |

A mod that requires a service this runtime does not publish is refused before it loads, with the
service and version named in the log. One that merely asks gets nothing and carries on.

## 🧩 The mods it ships

Two mods that do what a patcher used to do, so the patcher only has to bootstrap. Both are
32-bit only and both declare `wxl.patch` as required, so any other runtime refuses them cleanly.

| Mod | What it does |
|---|---|
| `wxl.glue-unlock` | bypasses the Lua/XML signature check, so custom interface code loads |
| `wxl.named-archives` | widens the client's `patch-?.MPQ` scan to `patch-*.MPQ`, so archives and folders with real names are mounted |

Both read memory before writing: if the bytes are already in the patched state they log
`already present` and do nothing.

## 📂 Layout

```
include/wxl/core/init.h          the 64-bit entry point, for the client to include
include/wxl/offsets/             site.hpp, layout.hpp and the generated tables
src/runtime/                     host bring-up, stages, the per-architecture attachment
src/hook/                        the detour engine and the named points        (x86)
src/services/                    wxl.hook, wxl.hook.raw, wxl.patch             (x86)
src/patcher/                     PE import and entry trampoline                (x86)
src/common/                      the log
mods/                            the mods above                                (x86)
deps/minhook/                    MinHook, BSD-2-Clause, unmodified             (x86)
tools/offsets/, tools/layouts/   regenerate the tables from the decompiled client
```

## 🔧 Troubleshooting

| Symptom | Cause |
|---|---|
| The game does not start, `0xc0000142` | `WarcraftXL.dll` is missing beside the executable, or is the wrong architecture |
| No log at all | the executable carries no WarcraftXL import: run the patcher |
| `the executable was patched by an older patcher` | it has the import but no trampoline: run the patcher again |
| A mod is missing from the log | wrong folder shape; it needs `Mods/<id>/mod.json` and `Mods/<id>/bin/<arch>/<name>.dll` |
| `refused … requires service …` | the mod needs something this build does not publish; see the table above |
| `not Wow.exe build 12340` | the addresses do not apply to this executable; named points and seams stay off |

## 📄 License

GPLv3. See [LICENSE](LICENSE).

MinHook, under `deps/minhook/`, is BSD-2-Clause and keeps its own license file.

<div align="center">

[⬆ Back to top](#warcraftxl-core)

</div>
