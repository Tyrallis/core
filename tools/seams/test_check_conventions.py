#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 WarcraftXL
#
# Self-test for check_conventions.py: a stub binary per failure mode, and the real 23 rows.
"""A checker nobody has seen fail is not known to work.

Two halves. The first serves a stub binary over a stub MCP -- a handful of synthetic functions
whose convention is known because this file wrote their instructions -- and asserts that the
tool reads each one right and says so when the row disagrees. The second points the tool at the
real client and compares its answer against the 23 rows verified by hand against the
disassembly; it is skipped, not failed, when the MCP is not up.
"""

import json
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
CHECKER = HERE / "check_conventions.py"

# --- A stub binary --------------------------------------------------------------------------
#
# One entry per function: its address, and its instructions as (size, text). The sizes are what
# the stub MCP prints as bytes and what the tool steps over, so they only have to be consistent.

def body(*instructions):
    return list(instructions)


CDECL_TWO = body((1, "push ebp"), (2, "mov ebp, esp"), (3, "mov eax, dword ptr [ebp + 8]"),
                 (3, "add eax, dword ptr [ebp + 0xc]"), (1, "pop ebp"), (1, "ret"))

STDCALL_TWO = body((1, "push ebp"), (2, "mov ebp, esp"), (3, "mov eax, dword ptr [ebp + 8]"),
                   (3, "add eax, dword ptr [ebp + 0xc]"), (1, "pop ebp"), (3, "ret 8"))

THISCALL_ONE = body((1, "push ebp"), (2, "mov ebp, esp"), (1, "push esi"), (2, "mov esi, ecx"),
                    (3, "mov eax, dword ptr [ebp + 8]"), (3, "mov dword ptr [esi + 4], eax"),
                    (1, "pop esi"), (1, "pop ebp"), (3, "ret 4"))

REGPARM_EDI = body((1, "push ebp"), (2, "mov ebp, esp"), (1, "push esi"),
                   (3, "lea esi, [edi + 0x10]"), (3, "mov eax, dword ptr [ebp + 8]"),
                   (1, "pop esi"), (1, "pop ebp"), (1, "ret"))

# A switch table right after the body: decoded linearly it reads registers the function never
# takes, which is what trimming at the table's address is for.
TABLE_SWITCH = body((3, "mov edx, dword ptr [esi + 0x14]"), (3, "cmp edx, 6"),
                    (2, "ja 0x401306"), (7, "jmp dword ptr [edx*4 + 0x401320]"),
                    (1, "push eax"), (1, "push ecx"), (2, "mov ecx, esi"),
                    (5, "call 0x409840"), (1, "ret"),
                    # From here on it is the table, decoded as whatever the bytes look like.
                    (2, "mov edi, edi"), (3, "add al, byte ptr [eax + 0x40]"),
                    (3, "add byte ptr [esi], dl"), (2, "inc eax"))

# Its caller: pushes two dwords and cleans them itself.
CALLER_CLEANS = body((1, "push ebp"), (2, "mov ebp, esp"), (2, "push 2"), (2, "push 1"),
                     (5, "call 0x401000"), (3, "add esp, 8"), (1, "pop ebp"), (1, "ret"))

# A caller MSVC folded the cleanup out of: it pushes and then adjusts much later.
CALLER_FOLDS = body((1, "push ebp"), (2, "mov ebp, esp"), (2, "push 2"), (2, "push 1"),
                    (5, "call 0x401000"), (2, "push 3"), (5, "call 0x409840"),
                    (3, "add esp, 0xc"), (1, "pop ebp"), (1, "ret"))

# An event handler that never reads the two parameters it is handed. The function alone reads
# no stack at all; only its caller says how many there are.
CDECL_IGNORES = body((1, "push ebp"), (2, "mov ebp, esp"), (5, "mov eax, 1"), (1, "pop ebp"),
                     (1, "ret"))

# Its caller pushes the two and cleans exactly those eight bytes: the evidence that excuses it.
# A call before it bounds the walk back over the pushes, as it does in real code.
CALLER_EXACT = body((1, "push ebp"), (2, "mov ebp, esp"), (5, "call 0x409840"), (2, "push 2"),
                    (2, "push 1"), (5, "call 0x401600"), (3, "add esp, 8"), (1, "pop ebp"),
                    (1, "ret"))

# The same evidence at the first call of its caller, where nothing bounds the walk and it runs
# on into the caller's own prologue: three pushes counted against eight bytes cleaned.
CDECL_IGNORES_FIRST = body((1, "push ebp"), (2, "mov ebp, esp"), (5, "mov eax, 1"), (1, "pop ebp"),
                           (1, "ret"))
CALLER_FIRST = body((1, "push ebp"), (2, "mov ebp, esp"), (2, "push 2"), (2, "push 1"),
                    (5, "call 0x401a00"), (3, "add esp, 8"), (1, "pop ebp"), (1, "ret"))

# The same handler reached only through a folded cleanup, which never shows the exact count.
CDECL_IGNORES_FOLDED = body((1, "push ebp"), (2, "mov ebp, esp"), (5, "mov eax, 1"),
                            (1, "pop ebp"), (1, "ret"))
CALLER_FOLDS_ONLY = body((1, "push ebp"), (2, "mov ebp, esp"), (2, "push 2"), (2, "push 1"),
                         (5, "call 0x401800"), (2, "push 3"), (5, "call 0x409840"),
                         (3, "add esp, 0xc"), (1, "pop ebp"), (1, "ret"))

PROGRAM = {
    0x00401000: CDECL_TWO,
    0x00401100: STDCALL_TWO,
    0x00401200: THISCALL_ONE,
    0x00401280: REGPARM_EDI,
    0x00401300: TABLE_SWITCH,
    0x00401400: CALLER_CLEANS,
    0x00401500: CALLER_FOLDS,
    0x00401600: CDECL_IGNORES,
    0x00401700: CALLER_EXACT,
    0x00401800: CDECL_IGNORES_FOLDED,
    0x00401900: CALLER_FOLDS_ONLY,
    0x00401A00: CDECL_IGNORES_FIRST,
    0x00401B00: CALLER_FIRST,
}

# (name, address, extent)
SYMBOLS = [("Thing::Plain", 0x00401000, 0x20), ("Thing::Owned", 0x00401100, 0x20),
           ("Thing::Member", 0x00401200, 0x20), ("Thing::Fast", 0x00401280, 0x20),
           ("Thing::Switch", 0x00401300, 0x30), ("Thing::CallsCleanly", 0x00401400, 0x20),
           ("Thing::CallsFolded", 0x00401500, 0x20), ("Thing::Ignores", 0x00401600, 0x20),
           ("Thing::CallsExactly", 0x00401700, 0x20), ("Thing::IgnoresFolded", 0x00401800, 0x20),
           ("Thing::CallsFoldedOnly", 0x00401900, 0x20), ("Thing::IgnoresFirst", 0x00401A00, 0x20),
           ("Thing::CallsFirst", 0x00401B00, 0x20)]

XREFS = {
    0x00401000: ["00401404(UNCONDITIONAL_CALL)"],   # inside CALLER_FOLDS, which folds; then
    0x00401100: [],
    0x00401200: [],
    0x00401280: [],
    0x00401300: [],
}


def flatten():
    """address -> (size, text), for every instruction of every stub function."""
    memory = {}
    for start, instructions in PROGRAM.items():
        address = start
        for size, text in instructions:
            memory[address] = (size, text)
            address += size
    return memory


MEMORY = flatten()
# The call sites, found by walking the stub callers; the tool reads them through ghidra_xrefs.
for _callee in (0x00401000, 0x00401600, 0x00401800, 0x00401A00):
    XREFS[_callee] = ["%08x(UNCONDITIONAL_CALL)" % a for a, (_, t) in sorted(MEMORY.items())
                      if t == "call 0x%x" % _callee]


class Stub(BaseHTTPRequestHandler):
    """Answers ghidra_disasm and ghidra_xrefs out of PROGRAM, the way the real MCP does."""

    def log_message(self, *_):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        message = json.loads(self.rfile.read(length) or b"{}")
        name = message.get("params", {}).get("name")
        arguments = message.get("params", {}).get("arguments", {})
        if name == "ghidra_disasm":
            text = self.disasm(int(arguments["addr"], 16), int(arguments.get("count", 16)))
        elif name == "ghidra_xrefs":
            address = int(arguments["addr"], 16)
            text = "to 0x%08X (%d): %s\n\nfrom 0x%08X (0): -" % (
                address, len(XREFS.get(address, [])),
                ", ".join(XREFS.get(address, [])) or "-", address)
        else:
            text = ""
        payload = json.dumps({"jsonrpc": "2.0", "id": message.get("id"),
                              "result": {"content": [{"type": "text", "text": text}],
                                         "isError": False}}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    @staticmethod
    def disasm(address, count):
        lines = []
        while len(lines) < count and address in MEMORY:
            size, text = MEMORY[address]
            lines.append("%08x  %s   %s" % (address, " ".join(["90"] * size), text))
            address += size
        return "\n".join(lines)


# --- The synthetic tree ---------------------------------------------------------------------

SEAMS_DEF = '''// A synthetic seam table.
#ifndef WXL_SEAM
#error "define WXL_SEAM(domain, name, symbol, ret, conv, params) before including seams.def"
#endif
#include "wxl/seams/demo.def"
'''

DEMO_DEF = '''// The demo domain.
#ifndef WXL_SEAM
#error "define WXL_SEAM(domain, name, symbol, ret, conv, params) before including demo.def"
#endif
WXL_SEAM(Demo, Plain, "Thing::Plain", void, cdecl, (int32_t a, int32_t b))
WXL_SEAM(Demo, Owned, "Thing::Owned", void, stdcall, (int32_t a, int32_t b))
WXL_SEAM(Demo, Member, "Thing::Member", void, thiscall, (void* self, int32_t a))
WXL_SEAM(Demo, Fast, "Thing::Fast", void, regparm, (void* context, int32_t a))
WXL_SEAM(Demo, Switch, "Thing::Switch", void, regparm, (void* object, const void* create, uint32_t time))
'''

THUNKS = '''#include "seams/RegparmX86.hpp"

__declspec(naked) static void FastThunk()
{
    __asm {
        push ebp
        mov  ebp, esp
        push edi
        push dword ptr [ebp-04h]
        call dword ptr [g_fastBefore]
        add  esp, 04h
        mov  edi, dword ptr [ebp-04h]
        call dword ptr [g_fastOriginal]
        ret
    }
}

__declspec(naked) static void SwitchThunk()
{
    __asm {
        push ebp
        mov  ebp, esp
        push esi
        push eax
        push ecx
        push dword ptr [ebp-04h]
        call dword ptr [g_switchBefore]
        add  esp, 04h
        mov  esi, dword ptr [ebp-04h]
        mov  eax, dword ptr [ebp-08h]
        mov  ecx, dword ptr [ebp-0Ch]
        call dword ptr [g_switchOriginal]
        ret
    }
}

namespace
{
    const wxl::core::seams::regparm::Row kRows[] = {
        { "Demo.Fast", reinterpret_cast<void*>(&FastThunk), &g_fastOriginal },
        { "Demo.Switch", reinterpret_cast<void*>(&SwitchThunk), &g_switchOriginal },
    };
}
'''


def functions_def():
    lines = ["// A synthetic offsets table."]
    for name, address, extent in SYMBOLS:
        lines.append('WXL_OFFSET_FUNCTION("%s", 0x%08X, 0x%X, 0x%X, "manual", "", "stub.cpp", 1)'
                     % (name, address, extent, extent))
    return "\n".join(lines) + "\n"


def build(case, demo_def=DEMO_DEF, thunks=THUNKS):
    for sub in ("seams", "offsets", "src"):
        (case / sub).mkdir(parents=True, exist_ok=True)
    (case / "seams" / "seams.def").write_text(SEAMS_DEF, encoding="utf-8")
    (case / "seams" / "demo.def").write_text(demo_def, encoding="utf-8")
    (case / "offsets" / "functions.def").write_text(functions_def(), encoding="utf-8")
    (case / "src" / "RegparmX86.cpp").write_text(thunks, encoding="utf-8")


def run(case, url, extra=()):
    return subprocess.run(
        [sys.executable, "-I", str(CHECKER),
         "--seams", str(case / "seams"), "--offsets", str(case / "offsets"),
         "--thunks", str(case / "src" / "RegparmX86.cpp"), "--mcp", url, *extra],
        capture_output=True, text=True)


# (label, demo.def, RegparmX86.cpp, exit code, text expected)
CASES = [
    ("a correct table", DEMO_DEF, THUNKS, 0, None),
    ("a regparm function declared cdecl",
     DEMO_DEF.replace('"Thing::Fast", void, regparm,', '"Thing::Fast", void, cdecl,'),
     THUNKS, 1, "declared cdecl, but 0x00401280 is regparm (EDI) + 1 stack dword(s)"),
    ("a thiscall function declared cdecl",
     DEMO_DEF.replace('"Thing::Member", void, thiscall, (void* self, int32_t a)',
                      '"Thing::Member", void, cdecl, (int32_t a)'),
     THUNKS, 1, "declared cdecl, but 0x00401200 is thiscall (ECX) + 1 stack dword(s)"),
    ("a stdcall function declared cdecl",
     DEMO_DEF.replace('"Thing::Owned", void, stdcall,', '"Thing::Owned", void, cdecl,'),
     THUNKS, 1, "declared cdecl, but 0x00401100 is stdcall, 2 stack dword(s)"),
    ("a cdecl function declared stdcall",
     DEMO_DEF.replace('"Thing::Plain", void, cdecl,', '"Thing::Plain", void, stdcall,'),
     THUNKS, 1, "declared stdcall, but 0x00401000 is cdecl, 2 stack dword(s)"),
    ("a parameter dropped",
     DEMO_DEF.replace('"Thing::Plain", void, cdecl, (int32_t a, int32_t b)',
                      '"Thing::Plain", void, cdecl, (int32_t a)'),
     THUNKS, 1, "the row's parameters fill 1 stack dword(s), but 0x00401000 reads 2"),
    ("a parameter invented",
     DEMO_DEF.replace('"Thing::Member", void, thiscall, (void* self, int32_t a)',
                      '"Thing::Member", void, thiscall, (void* self, int32_t a, int32_t b)'),
     THUNKS, 1, "the row's parameters fill 2 stack dword(s), but 0x00401200 reads 1"),
    ("a uint64 counted as one dword",
     DEMO_DEF.replace('"Thing::Plain", void, cdecl, (int32_t a, int32_t b)',
                      '"Thing::Plain", void, cdecl, (uint64_t guid)'),
     THUNKS, 0, None),
    ("a regparm row with the wrong number of registers",
     DEMO_DEF.replace('"Thing::Switch", void, regparm, (void* object, const void* create, '
                      'uint32_t time)',
                      '"Thing::Switch", void, regparm, (void* object, const void* create)'),
     THUNKS, 1, "takes 3 in registers (ESI, EAX, ECX) and 0 on the stack"),
    ("a regparm row with no thunk", DEMO_DEF,
     THUNKS.replace('        { "Demo.Fast", reinterpret_cast<void*>(&FastThunk), '
                    '&g_fastOriginal },\n', ""),
     1, "declared regparm, but RegparmX86.cpp has no thunk for it"),
    ("a thunk for a row that is not regparm",
     DEMO_DEF.replace('"Thing::Fast", void, regparm,', '"Thing::Fast", void, cdecl,'),
     THUNKS, 1, "has a thunk for Demo.Fast, but the row declares it cdecl"),
    ("a thunk for a row nobody declares", DEMO_DEF,
     THUNKS.replace('{ "Demo.Fast",', '{ "Demo.Ghost",'),
     1, "has a thunk for Demo.Ghost, which no row declares"),
    ("a thunk restoring the wrong register", DEMO_DEF,
     THUNKS.replace("        mov  edi, dword ptr [ebp-04h]", "        mov  esi, dword ptr [ebp-04h]"),
     1, "its thunk restores ESI before calling the original, but 0x00401280 reads EDI"),
    ("a symbol the offsets table does not carry",
     DEMO_DEF.replace('"Thing::Plain"', '"Thing::Vanished"'),
     THUNKS, 1, "no function named Thing::Vanished in functions.def"),
    # A cdecl event handler that ignores its parameters reads none of them, but they are on the
    # stack: the caller pushed them and takes them back. The caller's count decides -- and only
    # an exact one does.
    ("a cdecl handler that ignores what its caller hands it",
     DEMO_DEF + 'WXL_SEAM(Demo, Ignores, "Thing::Ignores", void, cdecl, '
                '(const void* data, void* param))\n',
     THUNKS, 0, None),
    ("the same, with no caller that cleans exactly what it declares",
     DEMO_DEF + 'WXL_SEAM(Demo, IgnoresFolded, "Thing::IgnoresFolded", void, cdecl, '
                '(const void* data, void* param))\n',
     THUNKS, 1, "the row's parameters fill 2 stack dword(s), but 0x00401800 reads 0"),
    ("a parameter invented on a handler that ignores its own",
     DEMO_DEF + 'WXL_SEAM(Demo, Ignores, "Thing::Ignores", void, cdecl, '
                '(const void* data, void* param, int32_t extra))\n',
     THUNKS, 1, "the row's parameters fill 3 stack dword(s), but 0x00401600 reads 0"),
    ("the same evidence at a caller's first call, which the walk over-counts",
     DEMO_DEF + 'WXL_SEAM(Demo, IgnoresFirst, "Thing::IgnoresFirst", void, cdecl, '
                '(const void* data, void* param))\n',
     THUNKS, 1, "pushes 3 and cleans 8"),
]

# What the real binary must say, verified by hand against the disassembly.
# point: (address, convention, registers, stack dwords)
GROUND_TRUTH = {
    "World.Preload":                   ("0x00781500", "cdecl", [], 2),
    "Event.DispatchUpdate":            ("0x0047DB70", "regparm", ["edi"], 2),
    "ObjectMgr.InitObject":            ("0x004D3FF0", "regparm", ["esi", "eax", "ecx"], 0),
    "ObjectMgr.ObjDelete":             ("0x004D6FC0", "regparm", ["eax"], 0),
    "ObjectMgr.DestroyRemovedObject":  ("0x004D4090", "regparm", ["esi"], 0),
    "Unit.OnDeath":                    ("0x00729220", "thiscall", ["ecx"], 0),
    "Unit.OnAuraAdded":                ("0x00727760", "thiscall", ["ecx"], 4),
    "Unit.OnAuraRemoved":              ("0x00722090", "thiscall", ["ecx"], 4),
    "Player.UpdateInventoryComponent": ("0x006E1E10", "thiscall", ["ecx"], 2),
    "GameUI.SendTarget":               ("0x00518DC0", "cdecl", [], 2),
    "GameUI.ClearTarget":              ("0x005241B0", "cdecl", [], 3),
    "MapArea.PrepareChunk":            ("0x007D6B30", "thiscall", ["ecx"], 2),
    "FrameScript.Initialize":          ("0x00819BB0", "cdecl", [], 1),
    "FrameScript.Destroy":             ("0x0081A9A0", "cdecl", [], 0),
    "FrameScript.CreateEvents":        ("0x0081B5F0", "cdecl", [], 2),
    "NetClient.ProcessMessage":        ("0x00631FE0", "thiscall", ["ecx"], 3),
    "NetClient.Send":                  ("0x00632B50", "thiscall", ["ecx"], 1),
    "NetClient.HandleConnect":         ("0x00632510", "thiscall", ["ecx"], 0),
    "NetClient.HandleDisconnect":      ("0x00632540", "thiscall", ["ecx"], 0),
    "Input.WindowProc":                ("0x0086A210", "cdecl", [], 4),
    "Storage.OpenFile":                ("0x00424B50", "stdcall", [], 4),
    "Client.Destroy":                  ("0x00402910", "cdecl", [], 0),
    "Sound.PlaySoundKit":              ("0x004C6A40", "cdecl", [], 8),
    # The Render family, read by hand before it went in. OnPaint reads none of its two: they are
    # pushed and cleaned by its caller (0x004A8BA0 `push 0; push 0; call; add esp, 8`), which is
    # why its row declares them and the binary reports none. GxScenePresent reads no [ebp+N].
    # IReleaseD3dResources takes `this` in ECX and one at [ebp+8], and ends in `ret 4`.
    # NotifyOnDeviceRestored and OnLayerRender read ECX before writing it and nothing on the
    # stack. RenderWorld reads its one at 0x004FB02C and ends in a bare `ret` at 0x004FB078.
    "Scrn.OnPaint":                    ("0x004A8720", "cdecl", [], 0),
    "Gx.ScenePresent":                 ("0x00682A00", "cdecl", [], 0),
    "Gx.ReleaseD3dResources":          ("0x00690150", "thiscall", ["ecx"], 1),
    "Gx.NotifyOnDeviceRestored":       ("0x006843B0", "thiscall", ["ecx"], 0),
    "SimpleTop.OnLayerRender":         ("0x00495410", "thiscall", ["ecx"], 0),
    "WorldFrame.RenderWorld":          ("0x004FAF90", "cdecl", [], 1),
    # Reads its title alone; the two flags are pushed and cleaned by its one caller
    # (0x00406B28 `push 1; push 1; push eax; call; add esp, 0xc`).
    "ConsoleDevice.Initialize":        ("0x0076AB80", "cdecl", [], 1),
    # The Spell, Item and UI rows. The two packet handlers are reached only through the handler
    # table and read their message at [ebp+0x14]; Spell_C_SpellFailed reads its cast at [ebp+8]
    # and its last argument at [ebp+0x1C]; Spell_C_CastSpell its caster at [ebp+8], the guid
    # two dwords, `confirmed` at [ebp+0x20]. UseItem takes `this` in ECX (0x00708C2A) and two
    # on the stack. vFrameScript_SignalEvent reads its index at [ebp+8] and the va_list at
    # [ebp+0x10]; FrameScript_HandleError is a lua_CFunction, the state at [ebp+8].
    "Spell.Packet_Group_30":           ("0x0080FEE0", "cdecl", [], 4),
    "Spell.Packet_SMSG_SPELL_FAILURE": ("0x00809C70", "cdecl", [], 4),
    "Spell.SpellFailed":               ("0x00808200", "cdecl", [], 6),
    "Spell.CastSpell":                 ("0x0080CCE0", "cdecl", [], 7),
    "Item.UseItem":                    ("0x00708C20", "thiscall", ["ecx"], 2),
    "FrameScript.vSignalEvent":        ("0x0081AC90", "cdecl", [], 3),
    "FrameScript.HandleError":         ("0x00819730", "cdecl", [], 1),
}

REAL_MCP = "http://127.0.0.1:8077/mcp"


def mcp_is_up(url):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": "ghidra_disasm",
                                  "arguments": {"addr": "0x00401000", "count": 1}}}).encode()
    request = urllib.request.Request(url, data=body, headers={
        "Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
    try:
        urllib.request.urlopen(request, timeout=10).read()
        return True
    except (urllib.error.URLError, OSError):
        return False


def report(label, ok, expected, output, failures):
    print("%-4s %s" % ("ok" if ok else "FAIL", label))
    if ok:
        return failures
    print("     expected %s" % expected)
    for line in output.strip().splitlines()[:12]:
        print("     | " + line)
    return failures + 1


def main():
    failures, total = 0, 0

    server = HTTPServer(("127.0.0.1", 0), Stub)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = "http://127.0.0.1:%d/mcp" % server.server_address[1]

    with tempfile.TemporaryDirectory() as temp:
        for index, (label, demo_def, thunks, want_code, want_text) in enumerate(CASES):
            total += 1
            case = Path(temp) / ("case%02d" % index)
            build(case, demo_def, thunks)
            result = run(case, url)
            output = result.stdout + result.stderr
            ok = result.returncode == want_code and (not want_text or want_text in output)
            failures = report(label, ok, "exit %d%s" % (want_code, ", with %r" % want_text
                                                        if want_text else ""), output, failures)

        # The tool must say so and leave, not crash, when the MCP is not there.
        total += 1
        case = Path(temp) / "unreachable"
        build(case)
        result = run(case, "http://127.0.0.1:1/mcp")
        output = result.stdout + result.stderr
        ok = result.returncode == 2 and "the ghidra MCP is not usable" in output
        failures = report("the MCP unreachable", ok, "exit 2, with the MCP named", output,
                          failures)
    server.shutdown()

    # --- the real client ---
    if not mcp_is_up(REAL_MCP):
        print("\nskip the 23 real rows: %s did not answer" % REAL_MCP)
        print("\n%d case(s), %d failure(s)" % (total, failures))
        return 1 if failures else 0

    result = subprocess.run([sys.executable, "-I", str(CHECKER), "--json"],
                            capture_output=True, text=True)
    try:
        answer = json.loads(result.stdout)
    except ValueError:
        print("FAIL the 23 real rows: the tool printed no JSON")
        print((result.stdout + result.stderr)[:2000])
        return 1
    total += 1
    failures = report("the real table has no disagreement", not answer["problems"],
                      "no problem", "\n".join(answer["problems"]), failures)

    rows = {row["point"]: row for row in answer["rows"]}
    for point, (address, convention, registers, stack) in GROUND_TRUTH.items():
        total += 1
        row = rows.get(point)
        got = None if row is None else (row["address"], row["convention"],
                                        row["registers"], row["stack_dwords"])
        ok = got == (address, convention, registers, stack)
        failures = report("%s reads as %s %s %d" % (point, convention,
                                                    ",".join(registers) or "-", stack),
                          ok, "%s" % ((address, convention, registers, stack),), str(got),
                          failures)
    for point in sorted(set(rows) - set(GROUND_TRUTH)):
        total += 1
        failures = report("%s is a row the ground truth does not cover" % point, False,
                          "a line for it in GROUND_TRUTH", "", failures)

    print("\n%d case(s), %d failure(s), full run %.1fs" % (total, failures, answer["seconds"]))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
