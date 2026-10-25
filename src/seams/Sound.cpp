// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Sound seam: what the runtime does as a sound is about to start.

#include "wxl/seams/seams.h"

#include <cstring>

#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    /// What the client answers for a sound it did not start because sound is off. A refused
    /// sound is told apart from a failed one by nothing here, and no caller looks.
    constexpr int32_t kNotPlayed = 17;
}

namespace wxl::seam::Sound::PlaySoundKit
{
    bool WXL_CALL Gate(int32_t* result, int32_t* sound_kit_id, const void** position,
                       void** object, void** properties, uint32_t* advanced_id,
                       uint32_t** sound_id, int32_t* check_distance, int32_t* keep_playing)
    {
        (void)position;
        (void)object;
        (void)properties;
        (void)advanced_id;
        (void)sound_id;
        (void)check_distance;
        (void)keep_playing;

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return false;

        wxl_bool        suppressed = WXL_FALSE;
        wxl_sound_args  args{};
        args.sound_id   = static_cast<uint32_t>(*sound_kit_id);
        args.path       = wxl_str{nullptr, 0};
        args.suppressed = &suppressed;

        host->Raise(WXL_EV_SoundPlay, &args);
        if (!suppressed) return false;

        *result = kNotPlayed;
        return true;
    }
}

namespace wxl::seam::Sound::CreateWaveData
{
    void WXL_CALL Before(void*, const char* file_name, uint32_t, void*, void*, void*, uint32_t, int32_t priority, int32_t,
                         uint32_t streaming, uint32_t, int32_t, float, float, float, const void*)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !file_name || !*file_name || !host->Listening(WXL_EV_SoundFileLoad)) return;

        wxl_sound_file_args args{};
        args.path      = wxl_str{file_name, static_cast<uint32_t>(strnlen(file_name, 260))};
        args.streaming = (streaming & 0xFFu) ? WXL_TRUE : WXL_FALSE; // a bool pushed as a dword
        args.priority  = priority;
        host->Raise(WXL_EV_SoundFileLoad, &args);
    }
}
