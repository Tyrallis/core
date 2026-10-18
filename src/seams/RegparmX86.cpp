// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The thunks that let a seam stand in front of a function whose arguments arrive in registers.
//
// Four functions of build 12340 have internal linkage and one or two callers, so MSVC was free
// to pass their arguments in registers and did. Each thunk here was written against that
// function's first instructions and against every one of its callers; the register each argument
// arrives in is recorded above the thunk with the addresses it was read from, so the claim can be
// checked rather than believed.
//
// Every thunk has the same shape. Save the register arguments into the frame before anything can
// touch them -- EAX and ECX are scratch, so the first C++ call would lose them. Call the seam's
// Before with them, if the row declares one. Put them back exactly as they came. Run the
// original. Call After, if the row declares one. Return the stack as the function's own
// convention leaves it: these four clean nothing, so the two that take stack arguments leave
// them to the caller, as the callers' `add esp` shows they expect.
//
// The thunks are naked because there is no way to say "this argument arrives in EDI" in C++, and
// no prologue the compiler writes could be trusted to leave the registers alone. That costs
// inline assembly, which exists on x86 only -- which is also the only architecture that needs it:
// on the recompiled client these seams are calls the source makes.

#include "seams/RegparmX86.hpp"

#include "wxl/seams/seams.h"

#include <cstdint>
#include <cstring>

// File scope and no namespace: a naked thunk takes no arguments, so everything it needs has to
// be reachable by a plain name the inline assembler can resolve. Each is initialised with a
// function's address, which is a constant, so none of this depends on static initialisation
// having run. The trampolines start null and the hook engine fills them in at attach.

static void(__cdecl* g_updateBefore)(void*, uint32_t, float) =
    &wxl::seam::Event::DispatchUpdate::Before;
static void* g_updateOriginal = nullptr;

static void(__cdecl* g_initAfter)(void*, const void*, uint32_t) =
    &wxl::seam::ObjectMgr::InitObject::After;
static void* g_initOriginal = nullptr;

static void(__cdecl* g_deleteBefore)(void*) = &wxl::seam::ObjectMgr::ObjDelete::Before;
static void* g_deleteOriginal               = nullptr;

static void(__cdecl* g_destroyBefore)(void*) =
    &wxl::seam::ObjectMgr::DestroyRemovedObject::Before;
static void* g_destroyOriginal = nullptr;

// --- Event.DispatchUpdate -----------------------------------------------------------------
// IEvtDispatchUpdate @0x0047DB70: the context arrives in EDI and nothing else does. Its body
// reads it before it reads anything at all (0x0047DB78 `lea esi,[edi+0x10]`, the context's
// critical section), and curr_time and elapsed_sec are at [ebp+8] and [ebp+0xC]. Its one caller
// at 0x0047F10D sets EDI immediately before the call and leaves the two stack slots to be
// cleaned with a later `add esp,0xc`, so the stack half is cdecl.
__declspec(naked) static void UpdateThunk()
{
    __asm {
        push ebp
        mov  ebp, esp
        push edi                        // [ebp-4]: the context, as the client handed it over
        push esi
        push ebx

        push dword ptr [ebp+0Ch]        // elapsed_sec
        push dword ptr [ebp+08h]        // curr_time
        push dword ptr [ebp-04h]        // context
        call dword ptr [g_updateBefore]
        add  esp, 0Ch

        push dword ptr [ebp+0Ch]
        push dword ptr [ebp+08h]
        mov  edi, dword ptr [ebp-04h]   // back in the register the original reads it from
        call dword ptr [g_updateOriginal]
        add  esp, 08h

        pop  ebx
        pop  esi
        pop  edi
        pop  ebp
        ret                             // cdecl: the caller cleans its two slots
    }
}

// --- ObjectMgr.InitObject -----------------------------------------------------------------
// InitObject @0x004D3FF0: no prologue, and `mov edx,[esi+0x14]` first. Its one caller at
// 0x004D6D63 sets ECX from its own argument and EAX to a local block in the two instructions
// before the call, and pushes nothing: object in ESI, create block in EAX, time in ECX. The
// body forwards the last two as stack arguments to the real constructor with ESI as `this`.
__declspec(naked) static void InitThunk()
{
    __asm {
        push ebp
        mov  ebp, esp
        push esi                        // [ebp-04h]: object
        push eax                        // [ebp-08h]: create block
        push ecx                        // [ebp-0Ch]: time
        push edi
        push ebx

        mov  esi, dword ptr [ebp-04h]
        mov  eax, dword ptr [ebp-08h]
        mov  ecx, dword ptr [ebp-0Ch]
        call dword ptr [g_initOriginal]

        push dword ptr [ebp-0Ch]
        push dword ptr [ebp-08h]
        push dword ptr [ebp-04h]
        call dword ptr [g_initAfter]
        add  esp, 0Ch

        pop  ebx
        pop  edi
        pop  ecx
        pop  eax
        pop  esi
        pop  ebp
        ret                             // nothing was pushed for it, so nothing is cleaned
    }
}

// --- ObjectMgr.ObjDelete ------------------------------------------------------------------
// ObjDelete @0x004D6FC0: a normal prologue, then `mov esi,eax` before anything is read. All
// three callers do `mov eax,esi` (at 0x004D737D, 0x004D76C0 and 0x004D786C) immediately before
// the call and push nothing.
__declspec(naked) static void DeleteThunk()
{
    __asm {
        push ebp
        mov  ebp, esp
        push eax                        // [ebp-04h]: the object, before a call can lose EAX
        push esi
        push edi
        push ebx

        push dword ptr [ebp-04h]
        call dword ptr [g_deleteBefore]
        add  esp, 04h

        mov  eax, dword ptr [ebp-04h]
        call dword ptr [g_deleteOriginal]

        pop  ebx
        pop  edi
        pop  esi
        pop  eax                        // unwinds the saved object; EAX is the caller's to lose
        pop  ebp
        ret
    }
}

// --- ObjectMgr.DestroyRemovedObject -------------------------------------------------------
// DestroyRemovedObject @0x004D4090: no prologue, and `mov ecx,[esi+8]` is the first thing it
// does with an argument. Its callers, 0x004D4919 among them, do `mov esi,<object>` before the
// call and push nothing.
__declspec(naked) static void DestroyThunk()
{
    __asm {
        push ebp
        mov  ebp, esp
        push esi                        // [ebp-04h]: the object
        push edi
        push ebx

        push dword ptr [ebp-04h]
        call dword ptr [g_destroyBefore]
        add  esp, 04h

        mov  esi, dword ptr [ebp-04h]
        call dword ptr [g_destroyOriginal]

        pop  ebx
        pop  edi
        pop  esi                        // the saved object is also the caller's ESI
        pop  ebp
        ret
    }
}

namespace
{
    const wxl::core::seams::regparm::Row kRows[] = {
        { "Event.DispatchUpdate", reinterpret_cast<void*>(&UpdateThunk), &g_updateOriginal },
        { "ObjectMgr.InitObject", reinterpret_cast<void*>(&InitThunk), &g_initOriginal },
        { "ObjectMgr.ObjDelete", reinterpret_cast<void*>(&DeleteThunk), &g_deleteOriginal },
        { "ObjectMgr.DestroyRemovedObject", reinterpret_cast<void*>(&DestroyThunk),
          &g_destroyOriginal },
    };
}

namespace wxl::core::seams::regparm
{
    const Row* Find(const char* name)
    {
        if (!name) return nullptr;
        for (const Row& row : kRows)
            if (std::strcmp(row.name, name) == 0) return &row;
        return nullptr;
    }
}
