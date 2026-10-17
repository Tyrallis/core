// wxl.object on this runtime: the one service instance and what the seams drive on it.
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

#include "objects/Objects.hpp"

namespace wxl::core::objects
{
    wxl::runtime::ObjectService& Service()
    {
        static ClientObjects                binding;
        static wxl::runtime::ObjectService service(binding);
        return service;
    }

    const wxl_object_api* Api() { return Service().Api(); }

    wxl_handle Track(void* object) { return Service().Track(object); }

    void Untrack(void* object) { Service().Untrack(object); }

    bool IsTracked(void* object) { return Service().HandleOf(object) != WXL_HANDLE_NONE; }

    wxl_handle HandleOf(void* object) { return Service().HandleOf(object); }

    wxl_handle FromGuid(uint64_t guid) { return Service().FromGuid(guid); }
}
