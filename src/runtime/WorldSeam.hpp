// The world seam: the detour that raises WXL_EV_WorldEnter.
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

#pragma once

namespace wxl::core::world
{
    /**
     * @brief Attaches the core's detour to the named point World.Preload.
     *
     * The detour raises WXL_EV_WorldEnter once the client's own function has returned and the
     * call was the one with the loading screen: the login and teleport path, not the preloads
     * around a cinematic or a far-sight target.
     * @return true when the detour is live.
     */
    bool Install();
}
