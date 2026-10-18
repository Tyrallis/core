#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 WarcraftXL
#
# Checks every seam row's declared calling convention against Wow.exe 3.3.5a (build 12340).
"""Verify the x86 half of the seam table against the client binary it attaches to.

check_seams.py compares the table with the C++ that implements it. Nothing compares the table
with Wow.exe, and a row that names the wrong convention produces a detour that clobbers a
register the original reads, or cleans a stack the caller also cleans. This reads the function's
prologue and its call sites out of the binary through the dawnlight-rag ghidra MCP, derives the
convention and the argument count from the instructions, and fails when the row disagrees.

The x64 side is direct source calls with no convention of its own, so only x86 is checked.
See README.md next to it.
"""

import argparse
import json
import re
import sys
import time
import urllib.error
import urllib.request
from bisect import bisect_right
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_seams as table

# --- The seam table, with the symbol column kept whole -----------------------------------------
#
# check_seams.Seam drops the "@0x..." of a symbol and keeps parameter names rather than their
# declarations. Resolving an address needs the first and sizing the stack needs the second, so the
# rows are read again here with check_seams' own scanner.

WIDE_TYPES = ("uint64_t", "int64_t", "unsigned long long", "long long", "double", "uint64",
              "int64", "__int64", "size64_t")


class Row:
    """One WXL_SEAM row, as the ABI sees it."""

    def __init__(self, domain, name, spec, returns, convention, params, origin, gated):
        self.domain = domain
        self.name = name
        self.spec = spec              # the symbol column, "name" or "name@0x<address>"
        self.returns = returns
        self.convention = convention
        self.params = params          # the parameter declarations, as written
        self.origin = origin
        self.gated = gated

    @property
    def point(self):
        return "%s.%s" % (self.domain, self.name)

    @property
    def symbol(self):
        return self.spec.split("@")[0]

    @property
    def dwords(self):
        """How many 4-byte slots the declared parameters occupy, `this` included."""
        total = 0
        for decl in self.params:
            total += 2 if self.is_wide(decl) else 1
        return total

    @staticmethod
    def is_wide(decl):
        if "*" in decl or "&" in decl:
            return False
        return any(word in decl for word in WIDE_TYPES)

    @property
    def expected_stack(self):
        """The stack dwords the row implies, or None when the row does not pin them down."""
        if self.convention == "thiscall":
            return max(0, self.dwords - 1)
        if self.convention == "regparm":
            return None               # the split between registers and stack is the binary's
        return self.dwords


def read_rows(seams_dir, problems):
    """Every row of the table, in include order, with its symbol column intact."""
    root = seams_dir / "seams.def"
    if not root.is_file():
        problems.append("the seam table is missing: %s" % root)
        return []

    files = [root]
    root_text = table.strip_cxx(root.read_text(encoding="utf-8", errors="replace"),
                                keep_strings=True, keep_directives=True)
    for match in table.INCLUDE_DEF.finditer(root_text):
        files.append(seams_dir / match.group(1))

    rows = []
    for path in files:
        if not path.is_file():
            problems.append("seams.def includes %s, which does not exist" % path.name)
            continue
        text = table.strip_cxx(path.read_text(encoding="utf-8", errors="replace"),
                               keep_strings=True)
        for match in table.SEAM_ROW.finditer(text):
            gated = match.group(1) is not None
            open_at = match.end() - 1
            close = table.balanced(text, open_at)
            if close < 0:
                continue
            columns = table.split_top_level(text[open_at + 1:close - 1])
            if len(columns) != 6:
                continue
            domain, name, spec, returns, convention, params = columns
            origin = "%s:%d" % (path.name, text.count("\n", 0, match.start()) + 1)
            inner = params.strip()[1:-1].strip()
            decls = [] if not inner else table.split_top_level(inner)
            rows.append(Row(domain, name, spec.strip().strip('"'), returns, convention,
                            decls, origin, gated))
    return rows


# --- The offsets table ------------------------------------------------------------------------

OFFSET_ROW = re.compile(r'WXL_OFFSET_FUNCTION\(\s*"([^"]*)"\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*'
                        r'(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,')
OFFSET_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


class Functions:
    """name -> address, and address -> the function that contains it."""

    def __init__(self):
        self.by_name = {}             # name -> {address: extent}
        self._starts = []             # sorted distinct addresses
        self._extent = {}             # address -> extent (length when known, else bound)

    @classmethod
    def load(cls, generated):
        """Every WXL_OFFSET_FUNCTION row of functions.def and the files it includes."""
        found = cls()
        todo, seen = [generated / "functions.def"], set()
        while todo:
            path = todo.pop()
            if path in seen or not path.is_file():
                continue
            seen.add(path)
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in OFFSET_INCLUDE.finditer(text):
                todo.append(generated / match.group(1))
            for match in OFFSET_ROW.finditer(text):
                name = match.group(1)
                address = int(match.group(2), 16)
                bound = int(match.group(3), 0)
                length = int(match.group(4), 0)
                extent = length or bound
                found.by_name.setdefault(name, {})[address] = extent
                # Rows can share an address (folded bodies); the widest extent wins.
                found._extent[address] = max(found._extent.get(address, 0), extent)
        found._starts = sorted(found._extent)
        return found

    def resolve(self, spec):
        """The (address, extent) a seam's symbol column names, as WXL_SEAM_SITE resolves it."""
        name, _, at = spec.partition("@")
        hits = self.by_name.get(name, {})
        if at:
            want = int(at, 16)
            hits = {a: e for a, e in hits.items() if a == want}
            if not hits:
                return None, "no function named %s sits at %s" % (name, at)
        if not hits:
            return None, "no function named %s in functions.def" % name
        if len(hits) > 1:
            return None, ("%s has %d addresses (%s); write it as \"<name>@0x...\""
                          % (name, len(hits), ", ".join("0x%08X" % a for a in sorted(hits))))
        address = next(iter(hits))
        extent = hits[address]
        if not extent:
            return None, "%s has no known extent (sentinel row)" % name
        return (address, extent), None

    def containing(self, address):
        """The start of the tagged function `address` falls inside, or None."""
        index = bisect_right(self._starts, address) - 1
        if index < 0:
            return None
        start = self._starts[index]
        if address >= start + self._extent[start]:
            return None
        return start, self._extent[start]


# --- The MCP ------------------------------------------------------------------------------------

class Unreachable(Exception):
    """The ghidra MCP did not answer; nothing can be checked against the binary."""


class Mcp:
    """The dawnlight-rag MCP over its HTTP transport, with an optional on-disk cache."""

    def __init__(self, url, cache_path=None, offline=False):
        self.url = url
        self.offline = offline
        self.cache_path = cache_path
        self.cache = {}
        self.calls = 0
        if cache_path and Path(cache_path).is_file():
            self.cache = json.loads(Path(cache_path).read_text(encoding="utf-8"))

    def call(self, tool, arguments):
        key = "%s %s" % (tool, json.dumps(arguments, sort_keys=True))
        if key in self.cache:
            return self.cache[key]
        if self.offline:
            raise Unreachable("the cache has no answer for `%s %s` and --offline forbids asking"
                              % (tool, arguments))
        body = {"jsonrpc": "2.0", "id": self.calls + 1, "method": "tools/call",
                "params": {"name": tool, "arguments": arguments}}
        request = urllib.request.Request(
            self.url, data=json.dumps(body).encode("utf-8"),
            headers={"Content-Type": "application/json",
                     "Accept": "application/json, text/event-stream"})
        try:
            raw = urllib.request.urlopen(request, timeout=120).read().decode("utf-8")
        except (urllib.error.URLError, OSError) as trouble:
            raise Unreachable("%s did not answer (%s)" % (self.url, trouble))
        self.calls += 1
        # The transport may answer as an event stream; the payload is the one data: line.
        if raw.lstrip().startswith("event:") or raw.lstrip().startswith("data:"):
            raw = "".join(line[5:].strip() for line in raw.splitlines()
                          if line.startswith("data:"))
        try:
            message = json.loads(raw)
        except ValueError:
            raise Unreachable("%s answered something that is not JSON-RPC" % self.url)
        if "error" in message:
            raise Unreachable("%s refused `%s`: %s" % (self.url, tool, message["error"]))
        result = message.get("result") or {}
        if result.get("isError"):
            raise Unreachable("the %s tool failed: %s" % (tool, result))
        text = "".join(part.get("text", "") for part in result.get("content", []))
        self.cache[key] = text
        return text

    def save(self):
        if self.cache_path:
            Path(self.cache_path).write_text(json.dumps(self.cache, indent=1, sort_keys=True),
                                             encoding="utf-8")


# --- Reading disassembly --------------------------------------------------------------------

DISASM_LINE = re.compile(r"^([0-9A-Fa-f]{6,8})\s+((?:[0-9A-Fa-f]{2}\s)+)\s*(\S+)\s*(.*?)\s*$")

PARENT = {"al": "eax", "ah": "eax", "ax": "eax", "bl": "ebx", "bh": "ebx", "bx": "ebx",
          "cl": "ecx", "ch": "ecx", "cx": "ecx", "dl": "edx", "dh": "edx", "dx": "edx",
          "si": "esi", "di": "edi", "bp": "ebp", "sp": "esp",
          "eax": "eax", "ebx": "ebx", "ecx": "ecx", "edx": "edx",
          "esi": "esi", "edi": "edi", "ebp": "ebp", "esp": "esp"}

REGISTER = re.compile(r"\b(e?[abcd]x|[abcd][lh]|e?(?:si|di|bp|sp))\b")
DISPLACEMENT = re.compile(r"\b(ebp|esp)\s*([+-])\s*(0x[0-9A-Fa-f]+|\d+)")

# The mnemonics whose first operand is written and not read, so a register there is not an
# argument arriving in it.
WRITE_FIRST = {"mov", "movzx", "movsx", "movabs", "lea", "pop", "xchg"}
# The mnemonics whose first operand is read and written both.
READ_WRITE_FIRST = {"add", "adc", "sub", "sbb", "and", "or", "xor", "shl", "shr", "sar", "rol",
                    "ror", "rcl", "rcr", "inc", "dec", "neg", "not", "imul", "mul", "idiv",
                    "div", "xadd", "bts", "btr", "btc", "shld", "shrd"}
IMMEDIATE = re.compile(r"^(0x[0-9A-Fa-f]+|-?\d+)$")


class Insn:
    __slots__ = ("addr", "size", "mn", "ops")

    def __init__(self, addr, size, mn, ops):
        self.addr = addr
        self.size = size
        self.mn = mn
        self.ops = ops

    def __repr__(self):
        return "%08x %s %s" % (self.addr, self.mn, self.ops)

    @property
    def text(self):
        return ("%s %s" % (self.mn, self.ops)).strip()

    @property
    def operands(self):
        return [part.strip() for part in self.ops.split(",")] if self.ops.strip() else []


def parse_disasm(text):
    """The instructions of a ghidra_disasm answer, in address order."""
    out = []
    for line in text.splitlines():
        match = DISASM_LINE.match(line.rstrip())
        if not match:
            continue
        size = len(match.group(2).split())
        out.append(Insn(int(match.group(1), 16), size, match.group(3).lower(),
                        match.group(4).lower()))
    return out


def disassemble(mcp, start, end, budget=40):
    """Every instruction from `start` up to `end`, decoded linearly from an instruction boundary.

    ghidra_disasm decodes from wherever it is pointed, so a wrong start yields plausible garbage.
    `start` must be a function entry or a call target. Several requests are chained when one does
    not reach `end`, each resuming on the boundary the previous one ended on.
    """
    insns, addr = [], start
    while addr < end and budget > 0:
        budget -= 1
        want = max(16, min(2000, (end - addr) // 2 + 8))
        chunk = [i for i in parse_disasm(mcp.call("ghidra_disasm",
                                                  {"addr": "0x%08X" % addr, "count": want}))
                 if i.addr >= addr]
        if not chunk:
            break
        insns.extend(i for i in chunk if i.addr < end)
        resume = chunk[-1].addr + chunk[-1].size
        if resume <= addr:
            break
        addr = resume
    return insns


def registers(operand):
    """The 32-bit registers an operand names, and whether it reaches memory."""
    memory = "[" in operand
    return {PARENT[m.group(1)] for m in REGISTER.finditer(operand) if m.group(1) in PARENT}, memory


HEX_IN_MEMORY = re.compile(r"\[[^]]*?(0x[0-9A-Fa-f]{6,8})[^]]*\]")


def trim_tables(insns, start, end):
    """The instructions up to the first switch table the function's own code points into.

    A `jmp dword ptr [reg*4 + 0x...]` is followed by its table, and a linear decode turns those
    addresses into plausible instructions that read registers the function never takes. Anything
    at or after the lowest such address inside the function is data.
    """
    limit = end
    for insn in insns:
        if insn.addr >= limit:
            break
        if insn.mn != "jmp" and "*" not in insn.ops:
            continue
        for match in HEX_IN_MEMORY.finditer(insn.ops):
            target = int(match.group(1), 16)
            if start < target < limit:
                limit = target
    return [i for i in insns if i.addr < limit]


# --- What the callee says ---------------------------------------------------------------------

VOLATILE = ("eax", "ecx", "edx")
PRESERVED = ("ebx", "esi", "edi")


class Callee:
    """What the function's own instructions say about how it was called."""

    def __init__(self):
        self.frame = False            # a push ebp / mov ebp, esp frame
        self.reg_args = []            # registers read before written, in first-read order
        self.reg_at = {}              # register -> the address it was first read at
        self.slots = {}               # stack dword index -> the address it was first read at
        self.rets = []                # (address, bytes the ret pops)
        self.notes = []

    @property
    def stack_dwords(self):
        """How many stack dwords the function reaches, counting a gap it skipped over."""
        widest = (max(self.slots) + 1) if self.slots else 0
        popped = max((n for _, n in self.rets), default=0) // 4
        return max(widest, popped)

    @property
    def cleans(self):
        """The bytes the function pops on return, or None when its returns disagree."""
        if not self.rets:
            return None
        amounts = {n for _, n in self.rets}
        if len(amounts) > 1:
            return None
        return amounts.pop()

    @property
    def convention(self):
        """The convention the instructions imply, or None when they do not settle it."""
        if self.reg_args and self.reg_args != ["ecx"]:
            return "regparm"
        if self.reg_args == ["ecx"]:
            return "thiscall"
        if self.cleans is None:
            # A function that ends in a tail call has no `ret` to read. With nothing on the
            # stack there is nothing to clean, and cdecl and stdcall are the same code.
            return "cdecl" if self.stack_dwords == 0 else None
        if self.cleans > 0:
            return "stdcall"
        return "cdecl"


def read_callee(insns):
    """Register arguments, stack arguments and stack cleanup, from the function's own code."""
    facts = Callee()
    if not insns:
        facts.notes.append("nothing decoded at the function's address")
        return facts

    # A register this function pushes and later pops is one it is saving, not one it was handed.
    saved = {reg for i in insns if i.mn == "pop" for reg in registers(i.ops)[0]} & set(PRESERVED)

    written, seen_call, esp = set(), False, 0
    prologue = True
    for insn in insns:
        ops = insn.operands
        first = ops[0] if ops else ""

        if prologue:
            # The entry run: the frame, the saves, and MSVC's `push ecx` for four bytes of frame.
            if insn.mn == "push" and first in ("ebp",) and not facts.frame:
                esp += 4
                continue
            if insn.mn == "mov" and first == "ebp" and len(ops) > 1 and ops[1] == "esp":
                facts.frame = True
                continue
            if insn.mn == "push" and first in PRESERVED:
                esp += 4
                continue
            if insn.mn == "push" and first in ("ecx", "eax") and facts.frame:
                esp += 4
                continue
            if insn.mn == "sub" and first == "esp" and len(ops) > 1 and IMMEDIATE.match(ops[1]):
                esp += int(ops[1], 0)
                continue
            prologue = False

        if insn.mn == "push":
            esp += 4
        elif insn.mn == "pop":
            esp -= 4
        elif insn.mn in ("sub", "add") and first == "esp" and len(ops) > 1 \
                and IMMEDIATE.match(ops[1]):
            esp += int(ops[1], 0) * (1 if insn.mn == "sub" else -1)

        if insn.mn in ("ret", "retn", "retf"):
            amount = int(insn.ops, 0) if IMMEDIATE.match(insn.ops.strip()) else 0
            facts.rets.append((insn.addr, amount))
            continue

        if insn.mn == "nop" or (insn.mn == "mov" and len(ops) == 2 and ops[0] == ops[1]):
            continue              # `mov edi, edi` is the padding MSVC aligns a function with

        # Reads and writes, operand by operand. A memory operand reads the registers that
        # address it whatever the mnemonic does with the value.
        reads, writes = set(), set()
        for index, operand in enumerate(ops):
            found, memory = registers(operand)
            if memory:
                reads |= found
            elif index == 0 and insn.mn in WRITE_FIRST:
                writes |= found
            elif index == 0 and insn.mn in READ_WRITE_FIRST:
                reads |= found
                writes |= found
            elif index == 0 and insn.mn.startswith("set"):
                writes |= found
            elif index == 0 and insn.mn.startswith("cmov"):
                reads |= found
                writes |= found
            else:
                reads |= found
        if insn.mn == "lea" and len(ops) > 1:
            # lea computes an address: its bracket registers are read, its first operand written.
            reads |= registers(ops[1])[0]
            writes |= registers(ops[0])[0]
            reads -= writes
        if insn.mn == "xor" and len(ops) == 2 and ops[0] == ops[1]:
            reads -= registers(ops[0])[0]
        if insn.mn == "push" and first in saved:
            reads -= {first}          # a save, however late the compiler placed it

        for reg in reads - written - {"esp", "ebp"}:
            # After a call, eax, ecx and edx hold whatever the callee left: reading one there
            # says nothing about how this function was entered.
            if reg in VOLATILE and seen_call:
                continue
            if reg not in facts.reg_args:
                facts.reg_args.append(reg)
                facts.reg_at[reg] = insn.addr
        written |= writes

        # Stack arguments. [ebp+N] is only meaningful once the frame is up; without one the
        # displacement is measured against the entry stack pointer.
        for match in DISPLACEMENT.finditer(insn.ops):
            base, sign, value = match.group(1), match.group(2), int(match.group(3), 0)
            if sign == "-":
                continue
            if base == "ebp" and facts.frame:
                entry = value - 4
            elif base == "esp" and not facts.frame:
                entry = value - esp
            else:
                continue
            if entry < 4 or entry > 4 * 64:
                continue
            index = (entry - 4) // 4
            facts.slots.setdefault(index, insn.addr)

        if insn.mn == "call":
            seen_call = True

    if not facts.rets and facts.stack_dwords:
        facts.notes.append("no `ret` was decoded inside its extent, so who cleans its %d stack "
                           "dword(s) is undetermined" % facts.stack_dwords)
    elif not facts.rets:
        facts.notes.append("it ends without a `ret` (a tail call), but it takes nothing on the "
                           "stack, so cdecl and stdcall are the same code here")
    elif len({n for _, n in facts.rets}) > 1:
        facts.notes.append("its returns pop different amounts (%s), so the cleanup is undetermined"
                           % ", ".join("0x%08X pops %d" % r for r in facts.rets))
    if not facts.frame and facts.slots:
        facts.notes.append("it has no ebp frame; the stack slots were read through esp and may be "
                           "off if a branch moved it")
    return facts


# --- What the callers say ---------------------------------------------------------------------

XREF_ENTRY = re.compile(r"\b([0-9A-Fa-f]{6,8})\(([A-Z_]+)\)")
BOUNDARY = ("call", "jmp", "ret", "retn", "leave", "int3")


class Site:
    def __init__(self, addr, pushes, cleanup, note):
        self.addr = addr
        self.pushes = pushes          # pushes counted immediately before the call
        self.cleanup = cleanup        # bytes the caller pops after the call, or None
        self.note = note


def callers_of(mcp, address):
    """The addresses that call `address`, from ghidra_xrefs; data references are not calls."""
    text = mcp.call("ghidra_xrefs", {"addr": "0x%08X" % address})
    head = text.split("from ")[0]
    return [int(m.group(1), 16) for m in XREF_ENTRY.finditer(head) if "CALL" in m.group(2)]


def read_site(mcp, functions, target, call_addr):
    """One call site: the pushes before the call and the cleanup after it, or why neither.

    The call's own address is an instruction boundary, but the instructions before it are not
    reachable by decoding backwards. The enclosing function is decoded from its entry instead, so
    every instruction before the call is the one the processor sees.
    """
    where = functions.containing(call_addr)
    if where is None:
        return Site(call_addr, None, None, "no tagged function contains it")
    start, extent = where
    insns = trim_tables(disassemble(mcp, start, start + extent), start, start + extent)
    at = next((n for n, i in enumerate(insns) if i.addr == call_addr), None)
    if at is None:
        return Site(call_addr, None, None,
                    "decoding 0x%08X from 0x%08X does not land on it" % (call_addr, start))
    if insns[at].mn == "jmp":
        return Site(call_addr, None, None,
                    "a tail call: the frame is the caller's, so no cleanup follows it")
    if insns[at].mn != "call":
        return Site(call_addr, None, None, "the instruction there is `%s`" % insns[at].text)
    if ("0x%x" % target) not in insns[at].ops:
        return Site(call_addr, None, None,
                    "the call there goes to `%s`, not the seam" % insns[at].ops)

    # Cleanup: `add esp, N`, or the `pop ecx` the compiler writes for one or two dwords. An
    # instruction that leaves esp alone may sit between, but a branch or a call ends the search.
    cleanup, step = None, at + 1
    popped = 0
    while step < len(insns) and step <= at + 4:
        insn = insns[step]
        if insn.mn == "add" and insn.operands[:1] == ["esp"] and len(insn.operands) > 1 \
                and IMMEDIATE.match(insn.operands[1]):
            cleanup = popped + int(insn.operands[1], 0)
            break
        if insn.mn == "pop" and insn.ops in ("ecx", "edx") and step == at + 1 + popped // 4:
            popped += 4
            cleanup = popped
            step += 1
            continue
        if insn.mn in BOUNDARY or insn.mn.startswith("j") or "esp" in insn.ops:
            break
        step += 1

    # Pushes: counted backwards to the previous boundary, so an interleaved `mov` does not hide
    # one. This is corroboration -- the callee's own `ret` is what settles who cleans.
    pushes, step = 0, at - 1
    while step >= 0 and step >= at - 24:
        insn = insns[step]
        if insn.mn == "push":
            pushes += 1
        elif insn.mn in BOUNDARY or insn.mn.startswith("j") or \
                (insn.mn in ("add", "sub") and insn.operands[:1] == ["esp"]):
            break
        step -= 1
    return Site(call_addr, pushes, cleanup, None)


# --- The regparm thunks -------------------------------------------------------------------------

THUNK_ROW = re.compile(r'\{\s*"([\w.]+)"\s*,\s*reinterpret_cast<void\*>\(&(\w+)\)')
THUNK_BODY = re.compile(r"__declspec\(naked\)\s+static\s+void\s+(\w+)\s*\(\s*\)\s*\{(.*?)\n\}",
                        re.S)
RESTORE = re.compile(r"^\s*mov\s+(eax|ebx|ecx|edx|esi|edi)\s*,\s*dword ptr \[ebp[+-]", re.M)
ORIGINAL_CALL = re.compile(r"call\s+dword ptr \[\s*g_\w*[Oo]riginal\s*\]")


def read_thunks(path, problems):
    """Each hand-written regparm thunk: the seam it stands for and the registers it restores."""
    if not path.is_file():
        problems.append("the regparm thunks are missing: %s" % path)
        return {}
    text = path.read_text(encoding="utf-8", errors="replace")
    bodies = {name: body for name, body in THUNK_BODY.findall(text)}
    thunks = {}
    for point, thunk in THUNK_ROW.findall(text):
        body = bodies.get(thunk)
        if body is None:
            problems.append("%s: the table names the thunk %s, which the file does not define"
                            % (path.name, thunk))
            continue
        # The registers put back in the instructions just before the original is called are the
        # ones the original reads them from.
        match = ORIGINAL_CALL.search(body)
        before = body[:match.start()] if match else body
        tail = before.rsplit("add  esp", 1)[-1]
        thunks[point] = [m.group(1) for m in RESTORE.finditer(tail)]
    return thunks


# --- The check ----------------------------------------------------------------------------------

class Finding:
    """One row's verdict: what the binary says, and whether the row agrees."""

    def __init__(self, row):
        self.row = row
        self.address = None
        self.callee = None
        self.sites = []
        self.problems = []
        self.notes = []

    def as_dict(self):
        """The row's verdict as data, for a caller that wants to compare rather than read."""
        callee = self.callee
        return {
            "point": self.row.point,
            "origin": self.row.origin,
            "symbol": self.row.spec,
            "address": "0x%08X" % self.address if self.address else None,
            "declared": self.row.convention,
            "declared_dwords": self.row.dwords,
            "convention": (callee.convention if callee else None),
            "registers": list(callee.reg_args) if callee else [],
            "stack_dwords": callee.stack_dwords if callee else None,
            "cleans": callee.cleans if callee else None,
            "callers": [{"address": "0x%08X" % s.addr, "pushes": s.pushes,
                         "cleanup": s.cleanup, "note": s.note} for s in self.sites],
            "notes": list(self.notes),
            "problems": list(self.problems),
        }

    @property
    def observed(self):
        if self.callee is None:
            return "unreadable"
        convention = self.callee.convention or "undetermined"
        if convention == "regparm":
            return "regparm (%s) + %d stack dword(s)" % (
                ", ".join(r.upper() for r in self.callee.reg_args), self.callee.stack_dwords)
        if convention == "thiscall":
            return "thiscall (ECX) + %d stack dword(s)" % self.callee.stack_dwords
        return "%s, %d stack dword(s)" % (convention, self.callee.stack_dwords)


def check_row(mcp, functions, row, max_callers):
    """One row against the binary: resolve, read the prologue, read the call sites, compare."""
    finding = Finding(row)
    resolved, trouble = functions.resolve(row.spec)
    if resolved is None:
        finding.problems.append("%s (%s): its symbol `%s` does not resolve: %s"
                                % (row.point, row.origin, row.spec, trouble))
        return finding
    address, extent = resolved
    finding.address = address

    insns = trim_tables(disassemble(mcp, address, address + extent), address, address + extent)
    callee = read_callee(insns)
    finding.callee = callee
    finding.notes.extend(callee.notes)

    # Call sites, until one of them cleans the stack explicitly. MSVC folds the cleanup of a call
    # into a later adjustment, so a site without one proves nothing and the next is tried.
    for caller in callers_of(mcp, address)[:max_callers]:
        site = read_site(mcp, functions, address, caller)
        finding.sites.append(site)
        if site.cleanup:
            break

    cleaning = [s for s in finding.sites if s.cleanup]
    observed = callee.convention

    # --- the convention ---
    if observed is None:
        finding.notes.append("the binary does not settle the convention; the row says %s"
                             % row.convention)
    elif observed != row.convention:
        # cdecl and stdcall part company only over who cleans, and with no stack to clean they
        # are the same machine code.
        same = (callee.stack_dwords == 0 and {observed, row.convention} <= {"cdecl", "stdcall"})
        if not same:
            finding.problems.append(
                "%s (%s): declared %s, but 0x%08X is %s%s"
                % (row.point, row.origin, row.convention, address, finding.observed,
                   evidence(callee, finding.sites)))

    # --- the argument count ---
    expected = row.expected_stack
    if expected is not None and observed is not None and callee.stack_dwords != expected:
        # A cdecl function that never reads its trailing parameters is still handed them: its
        # caller pushes them and its caller takes them back, so a detour declared with them
        # reads only what is really on the stack. What the callee reads is then a lower bound,
        # and the count that decides is the caller's. That is accepted on evidence alone -- a
        # call site that pushes exactly the declared count and cleans exactly that many bytes.
        #
        # Both witnesses are needed, because each can be wrong, and never in the same place.
        # The push count runs high at a caller's first call, where no earlier call bounds the
        # walk and it counts the caller's own prologue; but there, nothing earlier exists to be
        # folded, so the cleanup is exact and the two disagree. The cleanup runs high when MSVC
        # folds an earlier call's into it; but an earlier call is exactly what bounds the walk,
        # so the push count is exact and the two disagree again. Agreement is therefore real,
        # and a row declaring more than its caller pushes is still a parameter invented.
        unread = next((s for s in cleaning
                       if observed == "cdecl" and callee.stack_dwords < expected
                       and s.pushes == expected and s.cleanup == expected * 4), None)
        if unread:
            finding.notes.append(
                "0x%08X reads %d of its %d stack parameter(s); its caller at 0x%08X pushes all %d "
                "and cleans them, so the row's count stands"
                % (address, callee.stack_dwords, expected, unread.addr, expected))
        else:
            finding.problems.append(
                "%s (%s): the row's parameters fill %d stack dword(s), but 0x%08X reads %d%s"
                % (row.point, row.origin, expected, address, callee.stack_dwords,
                   evidence(callee, finding.sites)))
    if row.convention == "regparm" and observed == "regparm":
        total = len(callee.reg_args) + callee.stack_dwords
        if total != row.dwords:
            finding.problems.append(
                "%s (%s): the row declares %d parameter dword(s), but 0x%08X takes %d in "
                "registers (%s) and %d on the stack%s"
                % (row.point, row.origin, row.dwords, address, len(callee.reg_args),
                   ", ".join(r.upper() for r in callee.reg_args), callee.stack_dwords,
                   evidence(callee, finding.sites)))

    # --- who cleans ---
    if cleaning and observed in ("stdcall", "thiscall") and callee.stack_dwords:
        finding.problems.append(
            "%s (%s): 0x%08X cleans its own %d byte(s), but its caller at 0x%08X cleans %d too"
            % (row.point, row.origin, address, callee.cleans or 0,
               cleaning[0].addr, cleaning[0].cleanup))
    if cleaning:
        # A cleanup larger than this call needs is MSVC folding an earlier one into it, which
        # says nothing; one smaller than it needs does not add up and is worth a look.
        wanted = callee.stack_dwords * 4
        if cleaning[0].cleanup < wanted:
            finding.notes.append("its caller at 0x%08X cleans %d byte(s) where the function reads "
                                 "%d" % (cleaning[0].addr, cleaning[0].cleanup, wanted))
    elif observed == "cdecl" and callee.stack_dwords and finding.sites:
        finding.notes.append("no call site among %s cleans the stack explicitly, so cdecl rests "
                             "on the function's own `ret`"
                             % ", ".join("0x%08X" % s.addr for s in finding.sites))
    if not finding.sites:
        finding.notes.append("no call site: nothing calls 0x%08X directly, so only the "
                             "function's own code was read" % address)
    return finding


def evidence(callee, sites):
    """The addresses a verdict was read from, so the claim can be checked rather than believed."""
    parts = []
    for reg in callee.reg_args:
        parts.append("%s first read at 0x%08X" % (reg.upper(), callee.reg_at[reg]))
    if callee.slots:
        parts.append("stack slots read at %s"
                     % ", ".join("0x%08X" % callee.slots[i] for i in sorted(callee.slots)))
    for addr, amount in callee.rets[:2]:
        parts.append("`ret %d` at 0x%08X" % (amount, addr))
    for site in sites:
        if site.note:
            parts.append("caller 0x%08X unread (%s)" % (site.addr, site.note))
        elif site.cleanup:
            parts.append("caller 0x%08X pushes %d and cleans %d"
                         % (site.addr, site.pushes, site.cleanup))
        else:
            parts.append("caller 0x%08X pushes %d and cleans nothing" % (site.addr, site.pushes))
    return " [" + "; ".join(parts) + "]" if parts else ""


def check_thunks(rows, thunks, findings, problems, whole_table=True):
    """A regparm row needs a thunk, a thunk needs a regparm row, and both need the same registers.

    `whole_table` is false when --point narrowed the rows: a thunk whose row was filtered out is
    then not an orphan, so that half of the check is left to a full run.
    """
    declared = {row.point: row for row in rows}
    for point, row in sorted(declared.items()):
        if row.convention == "regparm" and point not in thunks:
            problems.append("%s (%s): declared regparm, but RegparmX86.cpp has no thunk for it; "
                            "the generated detour would be attached instead"
                            % (point, row.origin))
    for point in sorted(thunks):
        row = declared.get(point)
        if row is None:
            if whole_table:
                problems.append("RegparmX86.cpp has a thunk for %s, which no row declares" % point)
        elif row.convention != "regparm":
            problems.append("RegparmX86.cpp has a thunk for %s, but the row declares it %s; a "
                            "thunk is only attached to a regparm row" % (point, row.convention))
    for finding in findings:
        wanted = thunks.get(finding.row.point)
        if wanted is None or finding.callee is None or finding.callee.convention != "regparm":
            continue
        if set(wanted) != set(finding.callee.reg_args):
            problems.append(
                "%s (%s): its thunk restores %s before calling the original, but 0x%08X reads "
                "%s%s" % (finding.row.point, finding.row.origin,
                          ", ".join(r.upper() for r in wanted) or "nothing", finding.address,
                          ", ".join(r.upper() for r in finding.callee.reg_args),
                          evidence(finding.callee, finding.sites)))


def main(argv=None):
    here = Path(__file__).resolve().parent
    core = here.parent.parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seams", type=Path, default=core / "include" / "wxl" / "seams",
                        help="the directory holding seams.def and the domain tables")
    parser.add_argument("--offsets", type=Path,
                        default=core / "include" / "wxl" / "offsets" / "generated",
                        help="the directory holding the generated functions.def")
    parser.add_argument("--thunks", type=Path, default=core / "src" / "seams" / "RegparmX86.cpp",
                        help="the hand-written regparm thunks")
    parser.add_argument("--mcp", default="http://127.0.0.1:8077/mcp",
                        help="the dawnlight-rag MCP endpoint")
    parser.add_argument("--cache", type=Path, default=None,
                        help="a file to read and write the MCP's answers through")
    parser.add_argument("--offline", action="store_true",
                        help="answer only out of --cache; fail rather than reach the MCP")
    parser.add_argument("--max-callers", type=int, default=8,
                        help="how many call sites to read before giving up on finding a cleanup")
    parser.add_argument("--point", action="append", default=None,
                        help="check only this Domain.Name (repeatable)")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="print what the binary says about every row")
    parser.add_argument("--json", action="store_true",
                        help="print what the binary says about every row as JSON, on stdout")
    args = parser.parse_args(argv)

    problems = []
    rows = read_rows(args.seams.resolve(), problems)
    if not rows:
        print("check_conventions: no seam row was read from %s" % args.seams, file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 2
    if args.point:
        wanted = set(args.point)
        rows = [row for row in rows if row.point in wanted]
        missing = wanted - {row.point for row in rows}
        if missing:
            print("check_conventions: no such row: %s" % ", ".join(sorted(missing)),
                  file=sys.stderr)
            return 2

    if not (args.offsets / "functions.def").is_file():
        print("check_conventions: --offsets (%s) holds no functions.def" % args.offsets,
              file=sys.stderr)
        return 2
    functions = Functions.load(args.offsets.resolve())

    thunks = read_thunks(args.thunks.resolve(), problems)

    mcp = Mcp(args.mcp, args.cache, args.offline)
    started = time.monotonic()
    findings = []
    try:
        for row in rows:
            findings.append(check_row(mcp, functions, row, args.max_callers))
    except Unreachable as trouble:
        print("check_conventions: the ghidra MCP is not usable: %s" % trouble, file=sys.stderr)
        print("  start it with infra/deploy/rag-up.ps1 then infra/deploy/mcp-up.ps1, or point "
              "--mcp at it", file=sys.stderr)
        mcp.save()
        return 2
    elapsed = time.monotonic() - started
    mcp.save()

    check_thunks(rows, thunks, findings, problems, whole_table=not args.point)
    for finding in findings:
        problems.extend(finding.problems)

    if args.json:
        print(json.dumps({"rows": [f.as_dict() for f in findings],
                          "problems": problems, "seconds": round(elapsed, 3)}, indent=1))
    elif args.verbose:
        for finding in findings:
            print("%-32s %-12s %-10s %s"
                  % (finding.row.point,
                     "0x%08X" % finding.address if finding.address else "-",
                     finding.row.convention, finding.observed))
            for note in finding.notes:
                print("%34s %s" % ("", note))
            if finding.callee:
                print("%34s %s" % ("", evidence(finding.callee, finding.sites).strip(" []")))

    notes = sum(len(f.notes) for f in findings)
    if problems:
        print("check_conventions: %d disagreement(s) between the seam table and Wow.exe 12340"
              % len(problems), file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1

    if not args.json:
        print("check_conventions: %d row(s) agree with Wow.exe 12340 (%d regparm thunk(s), %d MCP "
              "call(s), %d undetermined point(s), %.1fs)"
              % (len(rows), len(thunks), mcp.calls, notes, elapsed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
