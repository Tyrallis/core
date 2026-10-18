// The Sound seam: what the runtime does as a sound is about to start.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "wxl/seams/seams.h"

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
