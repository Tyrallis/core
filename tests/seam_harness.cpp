// The seam harness: drives every seam's Before and After and checks a mod receives the events.
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
//
// What the game would do, without the game: this links the seam bodies themselves -- the same
// translation units both DLLs are built from -- against a real ModHost whose raisable events are
// the ones the seam table derives, subscribes a handler to each, calls the seams the way the
// client calls them, and checks what arrived.
//
// It cannot prove a seam is attached in a running client; that is what check_seams.py and
// WXL_SITE do. It proves the half in between: that a body raises what its row says, with the
// payload it says, to a mod that subscribed through the contract.

#include "seams/Table.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"
#include "wxl/seams/seams.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// --- What the seam bodies expect the runtime to provide --------------------------------------
//
// The bodies reach the client through these two and nothing else. The real ones live in
// Core.cpp and Attachment*.cpp, which carry an architecture's entry point and detours; here they
// are the harness's own, so the bodies under test are the product's and the rest is not.

namespace
{
    std::unique_ptr<wxl::host::ModHost> g_host;
    int32_t                             g_mapId = -1;
}

namespace wxl::core
{
    wxl::host::ModHost* Host() { return g_host.get(); }
}

namespace wxl::core::attachment
{
    int32_t MapId() { return g_mapId; }
}

// --- The mod ---------------------------------------------------------------------------------

namespace
{
    /// One event as a handler saw it: which, and a copy of the bytes it was handed.
    struct Received
    {
        wxl_event         event;
        std::vector<char> args;
    };

    std::vector<Received> g_received;

    /// The size of the struct each event carries, so the harness can keep a copy of it.
    size_t ArgsSize(wxl_event event)
    {
        switch (event)
        {
        case WXL_EV_Update:     return sizeof(wxl_update_args);
        case WXL_EV_WorldEnter:
        case WXL_EV_WorldLeave: return sizeof(wxl_world_args);
        default:                return 0;
        }
    }

    /// One tag per event id; a subscription's `user` is the address of its own, which is how a
    /// handler that is only handed `user` and `args` knows which event arrived.
    wxl_event g_tags[WXL_EV_COUNT]{};

    void WXL_CALL OnEvent(void* user, const void* args)
    {
        const wxl_event event = *static_cast<const wxl_event*>(user);
        Received record{ event, {} };
        const size_t size = ArgsSize(event);
        if (args && size)
        {
            record.args.resize(size);
            memcpy(record.args.data(), args, size);
        }
        g_received.push_back(std::move(record));
    }

    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++g_failures;
    }

    /// The one event received, or null when none or more than one arrived.
    const Received* Only(wxl_event event)
    {
        const Received* found = nullptr;
        for (const Received& record : g_received)
        {
            if (record.event != event) continue;
            if (found) return nullptr;
            found = &record;
        }
        return found;
    }
}

// --- The checks -------------------------------------------------------------------------------

namespace
{
    /// Every event the seam table says some body raises is one the host will let a mod subscribe
    /// to. A seam that raises an event the attachment never published is a silent dead event.
    void CheckEveryRaisedEventIsSubscribable()
    {
        for (const wxl_event event : wxl::core::seams::RaisedEvents())
        {
            g_tags[event] = event;
            const wxl_subscription subscription =
                g_host->Events().Subscribe(event, &OnEvent, &g_tags[event], 0);
            char what[128];
            snprintf(what, sizeof what, "a mod can subscribe to event %u", unsigned(event));
            Check(subscription != WXL_SUBSCRIPTION_NONE, what);
        }
    }

    /// Event.DispatchUpdate: Before raises Update carrying the tick's own elapsed_sec.
    void CheckEventDispatchUpdate()
    {
        g_received.clear();
        wxl::seam::Event::DispatchUpdate::Before(nullptr, 1000u, 0.125f);
        wxl::seam::Event::DispatchUpdate::After(nullptr, 1000u, 0.125f);

        const Received* update = Only(WXL_EV_Update);
        Check(update != nullptr, "Event.DispatchUpdate raises Update exactly once");
        if (!update) return;

        wxl_update_args args{};
        memcpy(&args, update->args.data(), sizeof args);
        Check(args.delta == 0.125f, "Update carries the seam's elapsed_sec as its delta");
    }

    /// World.Preload: the pair raises WorldEnter on the way in and WorldLeave only once a world
    /// has been entered, and neither when the loading screen is not being shown.
    void CheckWorldPreload()
    {
        const wxl_vec3 where{ 1.0f, 2.0f, 3.0f };
        g_mapId = 571;

        // A preload with no loading screen is a far-sight, cinematic or game-object preload.
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 0);
        wxl::seam::World::Preload::After(&where, 0);
        Check(g_received.empty(), "World.Preload raises nothing when show_loading is clear");

        // The first real preload enters a world; there is no world to leave yet.
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 1);
        wxl::seam::World::Preload::After(&where, 1);
        Check(Only(WXL_EV_WorldLeave) == nullptr, "the first world entry raises no WorldLeave");

        const Received* entered = Only(WXL_EV_WorldEnter);
        Check(entered != nullptr, "World.Preload raises WorldEnter once a world is loaded");
        if (entered)
        {
            wxl_world_args args{};
            memcpy(&args, entered->args.data(), sizeof args);
            Check(args.map_id == 571u, "WorldEnter carries the map the attachment reports");
            Check(args.position.x == 1.0f && args.position.y == 2.0f && args.position.z == 3.0f,
                  "WorldEnter carries the position the seam was called with");
        }

        // The next one leaves the world just entered, naming it, and enters the new one.
        g_mapId = 0;
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 1);
        const Received* left = Only(WXL_EV_WorldLeave);
        Check(left != nullptr, "the next world entry raises WorldLeave first");
        if (left)
        {
            wxl_world_args args{};
            memcpy(&args, left->args.data(), sizeof args);
            Check(args.map_id == 571u, "WorldLeave names the map being left, not the one arriving");
        }
        wxl::seam::World::Preload::After(&where, 1);
        Check(Only(WXL_EV_WorldEnter) != nullptr, "and WorldEnter after it");
    }
}

int main()
{
    wxl::host::HostConfig config;
    config.runtime_id   = "harness";
    config.runtime_name = "seam harness";
    config.events       = wxl::core::seams::RaisedEvents();
    g_host              = std::make_unique<wxl::host::ModHost>(std::move(config));

    printf("seam harness: %zu seam(s), %zu raisable event(s)\n",
           wxl::core::seams::Table().size(), wxl::core::seams::RaisedEvents().size());

    CheckEveryRaisedEventIsSubscribable();
    CheckEventDispatchUpdate();
    CheckWorldPreload();

    printf("\n%d failure(s)\n", g_failures);
    g_host.reset();
    return g_failures ? 1 : 0;
}
