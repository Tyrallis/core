// The x86 realisation of the client binding: what the attachment readies before the rows read.
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

namespace wxl::core::binding::x86
{
    /**
     * @brief Records the calling thread as the client's main thread.
     *
     * Call from the attachment's Prepare, which runs on the thread the entry trampoline runs on.
     * A row that reaches the object manager's thread-local state answers its absent value from
     * any other thread.
     */
    void Prepare();
}
