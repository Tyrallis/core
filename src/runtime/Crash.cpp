// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The crash report. A fault inside a mod's handler or entry point never gets here: the mod host's
// guard catches it and disables the mod. What reaches this filter is an exception nothing else
// handled, in the client or in code a mod left behind (a detour): the process is about to end.

#include "runtime/Crash.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Session.hpp"
#include "runtime/Stages.hpp"

#include "wxl/host/event_bus.hpp"
#include "wxl/host/guard.hpp"
#include "wxl/host/mod_host.hpp"

#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    using wxl::host::ModHost;

    constexpr const char* kTag = wxl::core::crash::kTag;
    constexpr int         kFrames = 24;

    std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_next{nullptr};
    LONG                                      g_reporting = 0;

    /// The first line of a report is flushed at once, in case the rest never comes.
    bool g_flushNext = true;

    /// The tag the report's lines go under: the crash's, or the client's fatal error's.
    const char* g_sayTag = kTag;

    void Say(const char* fmt, ...)
    {
        char    body[1536];
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(body, sizeof body, fmt, args);
        va_end(args);
        if (n < 0) return;
        const size_t len = static_cast<size_t>(n) < sizeof body ? static_cast<size_t>(n) : sizeof body - 1;
        wxl::log::WriteUrgent(g_sayTag, std::string_view(body, len), g_flushNext);
        g_flushNext = false;
    }

    void Frame(int index, uintptr_t pc, const char* how)
    {
        char where[192];
        wxl::host::DescribeAddress(pc, where, sizeof where);
        Say("#%d %s%s", index, where, how);
    }

#if defined(_M_IX86)
    /// Whether @p address lies in the code of a module: what a return address must.
    bool InModuleCode(uintptr_t address)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof region)) return false;
        const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        return region.State == MEM_COMMIT && region.Type == MEM_IMAGE && (region.Protect & executable) != 0;
    }
#endif

    /**
     * @brief The faulting thread's stack, from its context, a frame per line.
     *
     * x64 unwinds by the modules' unwind tables, as a debugger does. x86 follows the frame
     * pointers, which the client keeps, and when that chain stops short, lists the stack words
     * that point into a module's code: likely return addresses, marked as guesses.
     */
    void Stack(const CONTEXT& faulted)
    {
        CONTEXT context = faulted;
        int     frames  = 0;
        __try
        {
#if defined(_M_X64)
            for (; frames < kFrames && context.Rip; ++frames)
            {
                Frame(frames, static_cast<uintptr_t>(context.Rip), "");
                DWORD64 imageBase = 0;
                PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
                if (!function)
                {
                    // A leaf function: its return address is on top of the stack.
                    context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                    context.Rsp += 8;
                    continue;
                }
                PVOID   handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                                 &establisher, nullptr);
            }
#elif defined(_M_IX86)
            const NT_TIB* tib   = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
            const auto    low   = reinterpret_cast<uintptr_t>(tib->StackLimit);
            const auto    high  = reinterpret_cast<uintptr_t>(tib->StackBase);
            uintptr_t     frame = context.Ebp;
            Frame(frames++, context.Eip, "");
            while (frames < kFrames && frame >= low && frame + 8 <= high && (frame & 3) == 0)
            {
                const uintptr_t next   = reinterpret_cast<const uintptr_t*>(frame)[0];
                const uintptr_t caller = reinterpret_cast<const uintptr_t*>(frame)[1];
                if (!InModuleCode(caller)) break;
                Frame(frames++, caller, "");
                if (next <= frame) break;
                frame = next;
            }
            if (frames < 3)
            {
                int       found = 0;
                uintptr_t at    = context.Esp;
                for (int words = 0; words < 2048 && found < 16 && at >= low && at + 4 <= high; ++words, at += 4)
                {
                    const uintptr_t value = *reinterpret_cast<const uintptr_t*>(at);
                    if (!InModuleCode(value)) continue;
                    Frame(frames + found, value, " (found on the stack: a likely return address)");
                    ++found;
                }
            }
#endif
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Say("the stack stops here: reading it faulted");
        }
    }

    void Registers(const CONTEXT& c)
    {
#if defined(_M_X64)
        Say("registers rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX rsi=%016llX rdi=%016llX", c.Rax, c.Rbx, c.Rcx,
            c.Rdx, c.Rsi, c.Rdi);
        Say("registers rsp=%016llX rbp=%016llX r8=%016llX r9=%016llX r10=%016llX r11=%016llX", c.Rsp, c.Rbp, c.R8,
            c.R9, c.R10, c.R11);
#elif defined(_M_IX86)
        Say("registers eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX esp=%08lX ebp=%08lX", c.Eax, c.Ebx,
            c.Ecx, c.Edx, c.Esi, c.Edi, c.Esp, c.Ebp);
#endif
    }

    void Mods()
    {
        const ModHost* host = wxl::core::Host();
        if (!host)
        {
            Say("no mod host was up");
            return;
        }
        if (const ModHost::ModToken token = host->CurrentMod())
        {
            const wxl::host::LoadedMod* mod = host->ModOf(token);
            Say("this thread was running code of mod %s", mod ? mod->id.c_str() : "(loading)");
        }

        ModHost::RecentEvent recent[16];
        const size_t         count = host->Recent(recent, 16);
        if (count == 0)
        {
            Say("no event was raised to a mod before it");
            return;
        }
        char   list[1400];
        size_t used = 0;
        list[0]     = '\0';
        for (size_t i = 0; i < count; ++i)
        {
            const int wrote = std::snprintf(list + used, sizeof list - used, "%sOn%s (%s%u ms before)", i ? ", " : "",
                                            wxl::host::EventName(recent[i].event),
                                            recent[i].main_thread ? "" : "another thread, ", recent[i].ms_ago);
            if (wrote <= 0 || static_cast<size_t>(wrote) >= sizeof list - used) break;
            used += static_cast<size_t>(wrote);
        }
        Say("the last %zu events raised to mods, oldest first: %s", count, list);
    }

    void Report(EXCEPTION_POINTERS* info)
    {
        const EXCEPTION_RECORD* record  = info ? info->ExceptionRecord : nullptr;
        const CONTEXT*          context = info ? info->ContextRecord : nullptr;
        if (!record)
        {
            Say("unhandled exception, with no record of it");
            return;
        }

        wxl::host::Fault fault;
        fault.code    = record->ExceptionCode;
        fault.address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
        if ((fault.code == EXCEPTION_ACCESS_VIOLATION || fault.code == EXCEPTION_IN_PAGE_ERROR) &&
            record->NumberParameters >= 2)
        {
            fault.access     = static_cast<uint32_t>(record->ExceptionInformation[0]);
            fault.target     = static_cast<uintptr_t>(record->ExceptionInformation[1]);
            fault.has_target = true;
        }
        char what[256];
        wxl::host::DescribeFault(fault, what, sizeof what);
        char up[32];
        wxl::core::session::FormatDuration(wxl::core::session::UptimeMs(), up, sizeof up);
        const ModHost* host = wxl::core::Host();
        Say("unhandled %s, thread %lu%s, %s after launch, stage %s", what, GetCurrentThreadId(),
            host && host->OnMainThread() ? " (the main thread)" : "", up, wxl::core::stages::ReachedName());

        // A stack overflow leaves this thread almost no stack: the first line is all it gets.
        if (fault.code == EXCEPTION_STACK_OVERFLOW) return;

        if (context)
        {
            Registers(*context);
            Stack(*context);
        }
        Mods();
    }

    LONG WINAPI Filter(EXCEPTION_POINTERS* info)
    {
        // One report: a second thread faulting meanwhile, or the report itself, goes straight on.
        if (InterlockedExchange(&g_reporting, 1) == 0)
        {
            Report(info);
            const LPTOP_LEVEL_EXCEPTION_FILTER next = g_next.load();
            if (next)
            {
                char where[192];
                wxl::host::DescribeAddress(reinterpret_cast<uintptr_t>(next), where, sizeof where);
                Say("the crash handler set before this one runs next, at %s (on the client, Storm's: it "
                    "writes Errors\\)",
                    where);
            }
            else
            {
                Say("no other crash handler is set; the process ends here");
            }
            wxl::core::session::End("crash");
        }
        const LPTOP_LEVEL_EXCEPTION_FILTER next = g_next.load();
        return next && next != &Filter ? next(info) : EXCEPTION_CONTINUE_SEARCH;
    }

    /**
     * @brief The context of this function's caller, for a report written where nothing faulted.
     *
     * x86's RtlCaptureContext reads its caller's frame through EBP, which an optimised caller
     * may use as a plain register. Here the return address and the stack come from the return
     * slot, and EBP is only a hint the stack walk checks against the thread's stack before
     * following it, falling back to a scan of the stack.
     */
    __declspec(noinline) void Capture(CONTEXT& context)
    {
#if defined(_M_X64)
        RtlCaptureContext(&context);
#elif defined(_M_IX86)
        void** const slot = static_cast<void**>(_AddressOfReturnAddress());
        uintptr_t    frame = 0;
        __asm mov frame, ebp
        context.Eip = static_cast<DWORD>(reinterpret_cast<uintptr_t>(*slot));
        context.Esp = static_cast<DWORD>(reinterpret_cast<uintptr_t>(slot + 1));
        context.Ebp = static_cast<DWORD>(frame);
#endif
    }

    /// Copies a string the client handed over, which may be anything; "" when it cannot be read.
    void CopyClientText(const char* text, char* out, size_t capacity)
    {
        out[0] = '\0';
        if (!text) return;
        __try
        {
            size_t i = 0;
            for (; i + 1 < capacity && text[i]; ++i) out[i] = text[i];
            out[i] = '\0';
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out[0] = '\0';
        }
    }

    /// The client's error as the session's end line names it: its number and first line, short.
    void EndReason(uint32_t code, const char* message, char* out, size_t capacity)
    {
        char first[120];
        size_t n = 0;
        for (; n + 1 < sizeof first && message[n] && message[n] != '\n' && message[n] != '\r'; ++n) first[n] = message[n];
        first[n] = '\0';
        std::snprintf(out, capacity, "fatal error (#%u 0x%08X%s%s)", code & 0xFFFF, code, first[0] ? ": " : "", first);
    }

    /// For "Invalid function pointer: <hex>", where that pointer lies.
    void PointerHint(const char* message)
    {
        static const char kPrefix[] = "Invalid function pointer: ";
        if (std::strncmp(message, kPrefix, sizeof kPrefix - 1) != 0) return;
        const uintptr_t pointer =
            static_cast<uintptr_t>(std::strtoull(message + sizeof kPrefix - 1, nullptr, 16));
        char where[192];
        wxl::host::DescribeAddress(pointer, where, sizeof where);
        Say("the client was handed a function at %s and calls only its own .text or code the runtime "
            "registered (its image, the binaries the mod host loaded): whatever handed it over did not "
            "go through wxl::core::code",
            where);
    }
}

namespace wxl::core::crash
{
    void Install()
    {
        const LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(&Filter);
        if (!previous || previous == &Filter) return;
        g_next.store(previous);
        char where[192];
        wxl::host::DescribeAddress(reinterpret_cast<uintptr_t>(previous), where, sizeof where);
        WLOG_INFO(kTag, "the crash report runs first, then the filter at %s", where);
    }

    void ClientError(uint32_t code, const char* file, int32_t line, const char* message, bool recoverable)
    {
        char text[1024];
        char where[400];
        CopyClientText(message, text, sizeof text);
        CopyClientText(file, where, sizeof where);
        char from[440] = "";
        if (where[0] && line > 0)
            std::snprintf(from, sizeof from, ", at %s:%d", where, static_cast<int>(line));
        else if (where[0])
            std::snprintf(from, sizeof from, " (%s)", where);

        if (recoverable)
        {
            WLOG_WARN(kClientTag, "the client shows error #%u (0x%08X)%s and goes on: %s", code & 0xFFFF, code, from,
                      text);
            return;
        }
        // One report: the crash filter's (Storm's filter runs after it and displays the same
        // fault), or a first fatal error's.
        if (InterlockedExchange(&g_reporting, 1) != 0) return;

        g_sayTag    = kFatalTag;
        g_flushNext = true;
        char up[32];
        wxl::core::session::FormatDuration(wxl::core::session::UptimeMs(), up, sizeof up);
        const ModHost* host = wxl::core::Host();
        Say("fatal error #%u (0x%08X) shown by the client%s, thread %lu%s, %s after launch, stage %s: %s",
            code & 0xFFFF, code, from, GetCurrentThreadId(), host && host->OnMainThread() ? " (the main thread)" : "",
            up, wxl::core::stages::ReachedName(), text);
        PointerHint(text);

        CONTEXT context{};
        Capture(context);
        Stack(context);
        Mods();
        Say("the client writes Errors\\ and ends the process next");

        char reason[200];
        EndReason(code, text, reason, sizeof reason);
        wxl::core::session::End(reason);
        g_sayTag = kTag;
    }
}
