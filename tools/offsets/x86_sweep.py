#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 WarcraftXL
#
# Instruction-length decoding and function-end detection for extract_offsets.py.
"""
A length-only decoder for 32-bit x86 and a linear sweep that finds where a function ends.

The decoder knows instruction *lengths* and the few shapes the sweep needs (returns, jumps,
calls, int3, jump tables); it does not name instructions. It covers the one-byte map, the 0F
map (including SSE/SSE2 and the 0F 38 / 0F 3A maps) and x87, which is everything a 2008 MSVC
build of Wow.exe contains. An opcode it does not know makes the sweep give up on that
function rather than guess.

The sweep (see `function_end`) decodes forward from a function's start and stops at the int3
padding the linker puts between functions, using the ret/jmp structure to recognise a function
that ends exactly on an alignment boundary with no padding at all. Jump tables are read from
the image and skipped instead of being decoded as code. The result is either an end address
with the reason it was accepted, or None with the reason it was not.
"""

# --- one-byte opcode map -----------------------------------------------------------------------
# Each entry: (has_modrm, immediate). immediate is a byte count, or 'z' (4, or 2 under 66h),
# 'm' (moffs: 4, or 2 under 67h), 'f' (far pointer: 6, or 4 under 66h). None marks an
# undefined opcode or a prefix (prefixes are consumed before the table is consulted).

_ONE = {}


def _fill(table, codes, modrm, imm):
    for c in codes:
        table[c] = (modrm, imm)


# ALU blocks: op r/m,r; op r,r/m (x2 widths); op al,imm8; op eax,immz; then two one-byte ops.
for base in (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38):
    _fill(_ONE, range(base, base + 4), True, 0)
    _ONE[base + 4] = (False, 1)
    _ONE[base + 5] = (False, 'z')
    _ONE[base + 6] = (False, 0)   # push es / or 0E push cs / ... / 26 is a prefix (handled first)
    _ONE[base + 7] = (False, 0)   # pop es / 0F escape (handled first) / daa / ...
_fill(_ONE, range(0x40, 0x60), False, 0)          # inc/dec/push/pop reg
_ONE[0x60] = (False, 0)
_ONE[0x61] = (False, 0)
_ONE[0x62] = (True, 0)                            # bound
_ONE[0x63] = (True, 0)                            # arpl
_ONE[0x68] = (False, 'z')                         # push immz
_ONE[0x69] = (True, 'z')                          # imul r, r/m, immz
_ONE[0x6A] = (False, 1)                           # push imm8
_ONE[0x6B] = (True, 1)                            # imul r, r/m, imm8
_fill(_ONE, range(0x6C, 0x70), False, 0)          # ins/outs
_fill(_ONE, range(0x70, 0x80), False, 1)          # jcc rel8
_ONE[0x80] = (True, 1)
_ONE[0x81] = (True, 'z')
_ONE[0x82] = (True, 1)
_ONE[0x83] = (True, 1)
_fill(_ONE, range(0x84, 0x8F), True, 0)           # test/xchg/mov/lea/mov sreg
_ONE[0x8F] = (True, 0)                            # pop r/m
_fill(_ONE, range(0x90, 0x9A), False, 0)          # nop/xchg/cwde/cdq
_ONE[0x9A] = (False, 'f')                         # call far
_fill(_ONE, range(0x9B, 0xA0), False, 0)          # wait/pushf/popf/sahf/lahf
_fill(_ONE, range(0xA0, 0xA4), False, 'm')        # mov al/eax <-> moffs
_fill(_ONE, range(0xA4, 0xA8), False, 0)          # movs/cmps
_ONE[0xA8] = (False, 1)                           # test al, imm8
_ONE[0xA9] = (False, 'z')                         # test eax, immz
_fill(_ONE, range(0xAA, 0xB0), False, 0)          # stos/lods/scas
_fill(_ONE, range(0xB0, 0xB8), False, 1)          # mov r8, imm8
_fill(_ONE, range(0xB8, 0xC0), False, 'z')        # mov r32, immz
_ONE[0xC0] = (True, 1)                            # shift r/m8, imm8
_ONE[0xC1] = (True, 1)                            # shift r/m32, imm8
_ONE[0xC2] = (False, 2)                           # ret imm16
_ONE[0xC3] = (False, 0)                           # ret
_ONE[0xC4] = (True, 0)                            # les
_ONE[0xC5] = (True, 0)                            # lds
_ONE[0xC6] = (True, 1)                            # mov r/m8, imm8
_ONE[0xC7] = (True, 'z')                          # mov r/m32, immz
_ONE[0xC8] = (False, 3)                           # enter imm16, imm8
_ONE[0xC9] = (False, 0)                           # leave
_ONE[0xCA] = (False, 2)                           # retf imm16
_ONE[0xCB] = (False, 0)                           # retf
_ONE[0xCC] = (False, 0)                           # int3
_ONE[0xCD] = (False, 1)                           # int imm8
_ONE[0xCE] = (False, 0)                           # into
_ONE[0xCF] = (False, 0)                           # iret
_fill(_ONE, range(0xD0, 0xD4), True, 0)           # shift r/m, 1 / cl
_ONE[0xD4] = (False, 1)                           # aam
_ONE[0xD5] = (False, 1)                           # aad
_ONE[0xD6] = (False, 0)                           # salc
_ONE[0xD7] = (False, 0)                           # xlat
_fill(_ONE, range(0xD8, 0xE0), True, 0)           # x87
_fill(_ONE, range(0xE0, 0xE4), False, 1)          # loop/jecxz rel8
_fill(_ONE, range(0xE4, 0xE8), False, 1)          # in/out imm8
_ONE[0xE8] = (False, 'z')                         # call rel32
_ONE[0xE9] = (False, 'z')                         # jmp rel32
_ONE[0xEA] = (False, 'f')                         # jmp far
_ONE[0xEB] = (False, 1)                           # jmp rel8
_fill(_ONE, range(0xEC, 0xF0), False, 0)          # in/out dx
_ONE[0xF1] = (False, 0)                           # int1
_ONE[0xF4] = (False, 0)                           # hlt
_ONE[0xF5] = (False, 0)                           # cmc
_ONE[0xF6] = (True, 0)                            # grp3 r/m8 (imm8 when /0 or /1, added below)
_ONE[0xF7] = (True, 0)                            # grp3 r/m32 (immz when /0 or /1)
_fill(_ONE, range(0xF8, 0xFE), False, 0)          # clc..std
_ONE[0xFE] = (True, 0)                            # grp4
_ONE[0xFF] = (True, 0)                            # grp5

PREFIXES = {0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3}

# --- 0F opcode map -------------------------------------------------------------------------------

_TWO = {}
_TWO[0x00] = (True, 0)
_TWO[0x01] = (True, 0)
_TWO[0x02] = (True, 0)
_TWO[0x03] = (True, 0)
_fill(_TWO, (0x05, 0x06, 0x07, 0x08, 0x09, 0x0B), False, 0)
_TWO[0x0D] = (True, 0)                            # prefetch (3DNow hint)
_TWO[0x0F] = (True, 1)                            # 3DNow (modrm + suffix byte)
_fill(_TWO, range(0x10, 0x18), True, 0)           # SSE moves
_fill(_TWO, range(0x18, 0x20), True, 0)           # prefetch / multi-byte nop
_fill(_TWO, range(0x20, 0x24), True, 0)           # mov cr/dr
_fill(_TWO, range(0x28, 0x30), True, 0)           # movaps..comiss
_fill(_TWO, range(0x30, 0x36), False, 0)          # wrmsr/rdtsc/rdmsr/rdpmc/sysenter/sysexit
_TWO[0x37] = (False, 0)                           # getsec
_fill(_TWO, range(0x40, 0x50), True, 0)           # cmovcc
_fill(_TWO, range(0x50, 0x70), True, 0)           # SSE arithmetic, MMX
_TWO[0x70] = (True, 1)                            # pshuf*, imm8
_fill(_TWO, (0x71, 0x72, 0x73), True, 1)          # shift groups, imm8
_fill(_TWO, range(0x74, 0x77), True, 0)
_TWO[0x77] = (False, 0)                           # emms
_fill(_TWO, (0x78, 0x79), True, 0)                # vmread/vmwrite
_fill(_TWO, range(0x7C, 0x80), True, 0)           # haddpd.., movd/movq
_fill(_TWO, range(0x80, 0x90), False, 'z')        # jcc rel32
_fill(_TWO, range(0x90, 0xA0), True, 0)           # setcc
_fill(_TWO, (0xA0, 0xA1, 0xA2), False, 0)         # push/pop fs, cpuid
_TWO[0xA3] = (True, 0)                            # bt
_TWO[0xA4] = (True, 1)                            # shld imm8
_TWO[0xA5] = (True, 0)                            # shld cl
_fill(_TWO, (0xA8, 0xA9, 0xAA), False, 0)         # push/pop gs, rsm
_TWO[0xAB] = (True, 0)                            # bts
_TWO[0xAC] = (True, 1)                            # shrd imm8
_TWO[0xAD] = (True, 0)                            # shrd cl
_TWO[0xAE] = (True, 0)                            # fxsave.. / lfence.. (grp15)
_TWO[0xAF] = (True, 0)                            # imul
_fill(_TWO, range(0xB0, 0xB8), True, 0)           # cmpxchg, lss, btr, lfs, lgs, movzx
_TWO[0xB8] = (True, 0)                            # popcnt (F3) / jmpe
_TWO[0xB9] = (True, 0)                            # ud1
_TWO[0xBA] = (True, 1)                            # grp8 bt/bts/btr/btc imm8
_fill(_TWO, range(0xBB, 0xC0), True, 0)           # btc, bsf, bsr, movsx
_fill(_TWO, (0xC0, 0xC1), True, 0)                # xadd
_TWO[0xC2] = (True, 1)                            # cmpps imm8
_TWO[0xC3] = (True, 0)                            # movnti
_fill(_TWO, (0xC4, 0xC5, 0xC6), True, 1)          # pinsrw, pextrw, shufps
_TWO[0xC7] = (True, 0)                            # grp9 cmpxchg8b
_fill(_TWO, range(0xC8, 0xD0), False, 0)          # bswap
_fill(_TWO, range(0xD0, 0x100), True, 0)          # SSE2/MMX arithmetic (FF: ud0 with modrm)
del _TWO[0xFF]                                    # ud0 is never emitted by a compiler


# --- decoding ------------------------------------------------------------------------------------

class Insn:
    __slots__ = ('length', 'kind', 'target', 'table', 'byte_table')

    def __init__(self, length, kind, target=None, table=None, byte_table=None):
        self.length = length
        self.kind = kind            # 'ret' | 'jmp' | 'jcc' | 'call' | 'int3' | 'other'
        self.target = target        # absolute branch target for rel8/rel32 jumps and calls
        self.table = table          # disp32 of a `jmp [index*4 + disp32]` (a switch jump table)
        self.byte_table = byte_table  # disp32 of a `movzx r32, byte [reg + disp32]` (its index table)


def _modrm_length(data, pos, addr16):
    """Bytes used by the ModRM byte at pos and what follows it (SIB, displacement)."""
    if pos >= len(data):
        return None
    modrm = data[pos]
    mod = modrm >> 6
    rm = modrm & 7
    if mod == 3:
        return 1
    if addr16:
        if mod == 0:
            return 3 if rm == 6 else 1
        return 2 if mod == 1 else 3
    length = 1
    if rm == 4:
        if pos + 1 >= len(data):
            return None
        sib = data[pos + 1]
        length += 1
        if mod == 0 and (sib & 7) == 5:
            length += 4
    elif mod == 0 and rm == 5:
        length += 4
    if mod == 1:
        length += 1
    elif mod == 2:
        length += 4
    return length


def decode(data, pos):
    """Decode the instruction at data[pos]; returns an Insn or None for an unknown opcode."""
    n = len(data)
    i = pos
    opsize16 = False
    addr16 = False
    while i < n and data[i] in PREFIXES and i - pos < 14:
        if data[i] == 0x66:
            opsize16 = True
        elif data[i] == 0x67:
            addr16 = True
        i += 1
    if i >= n:
        return None
    op = data[i]
    i += 1
    kind = 'other'
    table = None
    byte_table = None
    rel = None
    if op == 0x0F:
        if i >= n:
            return None
        op2 = data[i]
        i += 1
        if op2 == 0x38:
            if i >= n:
                return None
            i += 1
            modrm, imm = True, 0
        elif op2 == 0x3A:
            if i >= n:
                return None
            i += 1
            modrm, imm = True, 1
        else:
            entry = _TWO.get(op2)
            if entry is None:
                return None
            modrm, imm = entry
            if 0x80 <= op2 <= 0x8F:
                kind = 'jcc'
                rel = 'z'
            elif op2 == 0xB6 or op2 == 0xB7:
                # movzx r32, [reg + disp32]: remember the displacement, it may index a byte table.
                if i < n and (data[i] >> 6) == 2 and (data[i] & 7) != 4 and i + 5 <= n:
                    byte_table = int.from_bytes(data[i + 1:i + 5], 'little')
    else:
        entry = _ONE.get(op)
        if entry is None:
            return None
        modrm, imm = entry
        if 0x70 <= op <= 0x7F or 0xE0 <= op <= 0xE3:
            kind = 'jcc'
            rel = 1
        elif op == 0xEB:
            kind = 'jmp'
            rel = 1
        elif op == 0xE9:
            kind = 'jmp'
            rel = 'z'
        elif op == 0xE8:
            kind = 'call'
            rel = 'z'
        elif op in (0xC2, 0xC3, 0xCA, 0xCB):
            kind = 'ret'
        elif op == 0xCC:
            kind = 'int3'
        elif op == 0xEA:
            kind = 'jmp'
        elif op == 0x9A:
            kind = 'call'
        elif op in (0xF6, 0xF7):
            if i < n and ((data[i] >> 3) & 7) in (0, 1):
                imm = 1 if op == 0xF6 else 'z'
        elif op == 0xFF:
            if i < n:
                reg = (data[i] >> 3) & 7
                if reg in (2, 3):
                    kind = 'call'
                elif reg in (4, 5):
                    kind = 'jmp'
                    # jmp [index*4 + disp32]: modrm 24h, SIB base 5 (disp32), scale 4.
                    if data[i] == 0x24 and i + 1 < n and (data[i + 1] & 0xC7) == 0x85 and i + 6 <= n:
                        table = int.from_bytes(data[i + 2:i + 6], 'little')
    if modrm:
        ml = _modrm_length(data, i, addr16)
        if ml is None:
            return None
        i += ml
    if imm == 'z':
        imm_len = 2 if opsize16 else 4
    elif imm == 'm':
        imm_len = 2 if addr16 else 4
    elif imm == 'f':
        imm_len = 4 if opsize16 else 6
    else:
        imm_len = imm
    if i + imm_len > n:
        return None
    target = None
    if rel is not None:
        if rel == 1:
            disp = int.from_bytes(data[i:i + 1], 'little', signed=True)
        elif opsize16:
            disp = int.from_bytes(data[i:i + 2], 'little', signed=True)
        else:
            disp = int.from_bytes(data[i:i + 4], 'little', signed=True)
        target = i + imm_len + disp     # relative to the data offset; the caller rebases
    i += imm_len
    return Insn(i - pos, kind, target, table, byte_table)


# --- the sweep -----------------------------------------------------------------------------------

ALIGN = 16              # the linker aligns function starts in this image to 16 bytes
MAX_TABLE_ENTRIES = 4096


def function_end(read, start, limit):
    """
    Find where the function at `start` ends, decoding forward but never past `limit` (the next
    tagged address in the section). `read(va, n)` returns bytes of the image at a virtual address.

    Returns (end, status): end is the exclusive end address, or None.

    status is one of
      'padding'     the decoded code stops right before an int3 run that reaches an alignment
                    boundary or the limit: the usual case
      'limit'       the code runs up to the limit without an int3 in between: the function fills
                    its bound (the next tagged function follows with no padding)
      'aligned'     a ret/jmp with nothing branching past it lands exactly on an alignment
                    boundary with code, not padding, after it: that code is either the next
                    (untagged) function or more of this one (an exception funclet, say); the
                    image alone cannot tell, so no end is claimed
      'undecodable' an opcode the decoder does not know, before any end was found
      'empty'       nothing decodes at start (the start byte is padding)
    """
    data = read(start, limit - start)
    size = len(data)
    if size == 0:
        return None, 'empty'
    pos = 0                      # offsets into data
    max_target = 0               # furthest forward branch target seen, as an offset
    tables = {}                  # table offset -> (end offset, entry count)
    byte_tables = set()
    while pos < size:
        if pos in tables:
            pos = tables[pos][0]
            continue
        if pos in byte_tables:
            # A byte index table that follows its dword table: entries are small case indices.
            counts = [c for (end, c) in tables.values() if end == pos]
            bound = max(counts) if counts else 256
            while pos < size and data[pos] < bound:
                pos += 1
            continue
        if data[pos] == 0xCC:
            run = pos
            while run < size and data[run] == 0xCC:
                run += 1
            if run == size or (start + run) % ALIGN == 0:
                if pos == 0:
                    return None, 'empty'
                return start + pos, 'padding'
            # A lone int3 inside code (a breakpoint helper): an ordinary instruction.
        insn = decode(data, pos)
        if insn is None:
            return None, 'undecodable'
        if insn.target is not None and insn.kind in ('jmp', 'jcc') and 0 <= insn.target < size:
            max_target = max(max_target, insn.target)
        if insn.table is not None and start <= insn.table < limit:
            toff = insn.table - start
            if toff not in tables:
                count = 0
                while count < MAX_TABLE_ENTRIES and toff + 4 * count + 4 <= size:
                    entry = int.from_bytes(data[toff + 4 * count:toff + 4 * count + 4], 'little')
                    if not (start <= entry < insn.table):
                        break
                    max_target = max(max_target, entry - start)
                    count += 1
                if count:
                    tables[toff] = (toff + 4 * count, count)
        if insn.byte_table is not None and start <= insn.byte_table < limit:
            byte_tables.add(insn.byte_table - start)
        pos += insn.length
        if insn.kind in ('ret', 'jmp') and pos > max_target:
            if pos >= size:
                return limit, 'limit'
            if data[pos] == 0xCC or pos in tables or pos in byte_tables:
                continue            # the padding check / table skip at the top of the loop
            if (start + pos) % ALIGN == 0:
                return None, 'aligned'
    return limit, 'limit'
