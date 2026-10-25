// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The wxl.m2 service: character appearances and the models mods hang on them.
//
// Every model a mod makes is kept here with the mod that made it, so a call can refuse a model
// it was never given, and a mod that is disabled leaves nothing hanging on a character. The set
// of models that follow their parent's bones is read on the client's animate threads; it only
// ever changes from mod calls, on the main thread, which never run while the scene animates.

#include "services/M2Service.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/guard.hpp"
#include "wxl/host/mod_host.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <exception>
#include <shared_mutex>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr char kTag[] = "m2";

    namespace binding = wxl::core::binding;

    struct Watcher
    {
        wxl_m2_equipment_fn fn;
        void*               user;
        uint32_t            owner;
        uint32_t            token;
        bool                live;
    };

    struct Model
    {
        uint32_t owner;
        bool     follow;
        void*    file; // its CM2Shared: what its interface copies are known by
        wxl::core::services::m2::Pose pose{};
        bool     world = false; // placed in the world: it has a map entity to take down
    };

    std::vector<Watcher>              g_watchers;
    uint32_t                          g_live      = 0;
    uint32_t                          g_nextToken = 1;
    int                               g_depth     = 0; // dispatches in progress: no compaction under them
    std::unordered_map<void*, Model>  g_models;
    uint32_t                          g_following = 0;
    struct FilePose
    {
        uint32_t                      count; // how many posed models are made of the file
        wxl::core::services::m2::Pose pose;  // the last one's: copies take it
    };
    std::unordered_map<void*, FilePose> g_followFiles;

    // --- Bone watchers: called on the animate threads ----------------------------------------
    //
    // A fixed table, so the animate threads never see it move: a slot is filled on the main thread
    // and only ever switched off after. Which watchers want a file is decided once per file and
    // kept here, under a lock both animate threads take; a change to the watchers starts it over.

    constexpr uint32_t kBoneSlots = 16;

    struct BoneWatcher
    {
        wxl_m2_bones_filter_fn filter = nullptr;
        wxl_m2_bones_fn        fn     = nullptr;
        void*                  user   = nullptr;
        uint32_t               owner  = 0;
        uint32_t               token  = 0;
        std::atomic<bool>      live{false};
    };

    BoneWatcher           g_bones[kBoneSlots];
    std::atomic<uint32_t> g_boneLive{0};
    std::atomic<uint32_t> g_boneEpoch{1};

    struct FileWant
    {
        std::string path;
        uint32_t    epoch = 0;
        uint32_t    mask  = 0; // bit per slot of g_bones
    };
    std::shared_mutex                   g_wantLock;
    std::unordered_map<void*, FileWant> g_wants;

    void DropBones(BoneWatcher& watcher)
    {
        if (!watcher.live.exchange(false)) return;
        --g_boneLive;
        ++g_boneEpoch;
    }

    struct BoneCall
    {
        const BoneWatcher*  watcher;
        const wxl_m2_bones* bones;
        wxl_bool            wanted;
    };

    void InvokeBoneFilter(void* context)
    {
        BoneCall* call = static_cast<BoneCall*>(context);
        call->wanted = call->watcher->filter
                           ? call->watcher->filter(call->bones->file, call->bones->path, call->bones->bone_count,
                                                   call->watcher->user)
                           : WXL_TRUE;
    }

    void InvokeBoneWatcher(void* context)
    {
        const BoneCall* call = static_cast<const BoneCall*>(context);
        call->watcher->fn(call->bones, call->watcher->user);
    }

    /// Calls one bone watcher under the guard; a fault switches it off.
    bool GuardedBones(BoneWatcher& watcher, void (*fn)(void*), BoneCall& call)
    {
        wxl::host::Fault fault;
        if (wxl::host::CallGuarded(fn, &call, &fault)) return true;
        char what[96];
        wxl::host::DescribeFault(fault, what, sizeof what);
        WLOG_ERROR(kTag, "a bone watcher faulted and was dropped: %s", what);
        DropBones(watcher);
        return false;
    }

    /// The watchers that want `bones`' file: asked once per file, kept until the watchers change.
    uint32_t WantsOf(const wxl_m2_bones& bones)
    {
        const uint32_t         epoch = g_boneEpoch.load();
        const std::string_view path(bones.path.data ? bones.path.data : "", bones.path.size);
        {
            std::shared_lock lock(g_wantLock);
            auto             it = g_wants.find(bones.file);
            if (it != g_wants.end() && it->second.epoch == epoch && it->second.path == path) return it->second.mask;
        }
        uint32_t mask = 0;
        for (uint32_t i = 0; i < kBoneSlots; ++i)
        {
            BoneWatcher& watcher = g_bones[i];
            if (!watcher.live.load()) continue;
            BoneCall call{&watcher, &bones, WXL_FALSE};
            if (GuardedBones(watcher, &InvokeBoneFilter, call) && call.wanted) mask |= 1u << i;
        }
        std::unique_lock lock(g_wantLock);
        FileWant&        want = g_wants[bones.file];
        want.path.assign(path);
        want.epoch = epoch;
        want.mask  = mask;
        return mask;
    }

    struct WatcherCall
    {
        const Watcher*          watcher;
        const wxl_m2_equipment* change;
    };

    void InvokeWatcher(void* context)
    {
        const WatcherCall* call = static_cast<const WatcherCall*>(context);
        try
        {
            call->watcher->fn(call->change, call->watcher->user);
        }
        catch (const std::exception& e)
        {
            WLOG_ERROR(kTag, "an equipment watcher threw: %s", e.what());
        }
        catch (...)
        {
            WLOG_ERROR(kTag, "an equipment watcher threw");
        }
    }

    void Compact()
    {
        if (g_depth != 0) return;
        std::erase_if(g_watchers, [](const Watcher& watcher) { return !watcher.live; });
    }

    void Drop(Watcher& watcher)
    {
        if (!watcher.live) return;
        watcher.live = false;
        --g_live;
    }

    uint32_t CurrentMod()
    {
        wxl::host::ModHost* host = wxl::core::Host();
        return host ? host->CurrentMod() : 0;
    }

    Model* Known(void* model)
    {
        if (!model) return nullptr;
        auto it = g_models.find(model);
        return it == g_models.end() ? nullptr : &it->second;
    }

    /// Sets how a model is posed each frame; a pose of kind None leaves it as the client poses it.
    void SetPose(Model& model, const wxl::core::services::m2::Pose& pose)
    {
        using wxl::core::services::m2::Pose;
        const bool follow = pose.kind != Pose::None;
        if (model.follow)
        {
            --g_following;
            if (model.file && --g_followFiles[model.file].count == 0) g_followFiles.erase(model.file);
        }
        model.follow = follow;
        model.pose   = pose;
        if (!follow) return;
        ++g_following;
        if (!model.file) return;
        FilePose& file = g_followFiles[model.file];
        ++file.count;
        file.pose = pose;
    }

    void SetFollow(Model& model, bool follow)
    {
        wxl::core::services::m2::Pose pose{};
        pose.kind = follow ? wxl::core::services::m2::Pose::Bones : wxl::core::services::m2::Pose::None;
        SetPose(model, pose);
    }

    /// Detaches and lets go of one model this service made, and forgets it.
    void Forget(std::unordered_map<void*, Model>::iterator it)
    {
        void* model = it->first;
        SetFollow(it->second, false);
        const bool world = it->second.world;
        g_models.erase(it);
        if (world) binding::m2_unplace_world(model);
        binding::m2_detach_model(model);
        binding::m2_release_model(model);
    }

    std::string Text(wxl_str text) { return text.data ? std::string(text.data, text.size) : std::string(); }

    // --- The table ----------------------------------------------------------------------------

    wxl_result WXL_CALL WatchEquipment(wxl_m2_equipment_fn fn, void* user, uint32_t* out_token)
    {
        if (!fn) return WXL_E_INVALID;
        const uint32_t token = g_nextToken++;
        g_watchers.push_back(Watcher{fn, user, CurrentMod(), token, true});
        ++g_live;
        if (out_token) *out_token = token;
        return WXL_OK;
    }

    void WXL_CALL UnwatchEquipment(uint32_t token)
    {
        for (Watcher& watcher : g_watchers)
            if (watcher.token == token) Drop(watcher);
        Compact();
    }

    wxl_result WXL_CALL WatchBones(wxl_m2_bones_filter_fn filter, wxl_m2_bones_fn fn, void* user, uint32_t* out_token)
    {
        if (!fn) return WXL_E_INVALID;
        for (BoneWatcher& watcher : g_bones)
        {
            if (watcher.live.load()) continue;
            watcher.filter = filter;
            watcher.fn     = fn;
            watcher.user   = user;
            watcher.owner  = CurrentMod();
            watcher.token  = g_nextToken++;
            watcher.live.store(true);
            ++g_boneLive;
            ++g_boneEpoch;
            if (out_token) *out_token = watcher.token;
            return WXL_OK;
        }
        return WXL_E_FAILED;
    }

    void WXL_CALL UnwatchBones(uint32_t token)
    {
        for (BoneWatcher& watcher : g_bones)
            if (watcher.token == token) DropBones(watcher);
    }

    wxl_result WXL_CALL BoneInfo(void* file, uint32_t index, int16_t* out_parent, wxl_vec3* out_pivot)
    {
        if (!file) return WXL_E_INVALID;
        return binding::m2_file_bone(file, index, out_parent, out_pivot) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    wxl_result WXL_CALL ItemDisplay(uint32_t display_id, wxl_m2_item_display* out)
    {
        if (!out || display_id == 0) return WXL_E_INVALID;
        wxl_m2_item_display row{};
        row.struct_size = sizeof row;
        if (!binding::m2_item_display(display_id, &row)) return WXL_E_NOT_FOUND;
        *out = row;
        return WXL_OK;
    }

    wxl_result WXL_CALL RacePrefix(uint32_t race_id, wxl_str* out)
    {
        if (!out) return WXL_E_INVALID;
        const char* prefix = binding::m2_race_prefix(race_id);
        if (!prefix) return WXL_E_NOT_FOUND;
        out->data = prefix;
        out->size = static_cast<uint32_t>(std::char_traits<char>::length(prefix));
        return WXL_OK;
    }

    void* WXL_CALL CreateModel(void* scene_model, wxl_str path)
    {
        const std::string file = Text(path);
        if (!scene_model || file.empty()) return nullptr;
        void* model = binding::m2_create_model(scene_model, file.c_str());
        if (model) g_models[model] = Model{CurrentMod(), false, binding::m2_model_file(model)};
        return model;
    }

    void WXL_CALL ReleaseModel(void* model)
    {
        auto it = g_models.find(model);
        if (it != g_models.end()) Forget(it);
    }

    wxl_result WXL_CALL AttachModel(void* model, void* parent, uint32_t attachment, uint32_t flags)
    {
        Model* known = Known(model);
        if (!known || !parent || parent == model) return WXL_E_INVALID;
        const wxl_bool to_root = (flags & WXL_M2_ATTACH_ROOT) ? WXL_TRUE : WXL_FALSE;
        if (known->world)
        {
            binding::m2_unplace_world(model);
            known->world = false;
        }
        if (!binding::m2_attach_model(model, parent, attachment, to_root)) return WXL_E_NOT_FOUND;
        SetFollow(*known, (flags & WXL_M2_ATTACH_FOLLOW_BONES) != 0);
        return WXL_OK;
    }

    void WXL_CALL DetachModel(void* model)
    {
        Model* known = Known(model);
        if (!known) return;
        SetFollow(*known, false);
        binding::m2_detach_model(model);
    }

    void* WXL_CALL ModelParent(void* model)
    {
        return Known(model) ? binding::m2_model_parent(model) : nullptr;
    }

    void WXL_CALL SetGeosets(void* model, uint32_t first, uint32_t last, wxl_bool visible)
    {
        if (Known(model) && first <= last) binding::m2_set_geosets(model, first, last, visible);
    }

    wxl_result WXL_CALL ReplaceTexture(void* model, uint32_t type, wxl_str path)
    {
        const std::string file = Text(path);
        if (!Known(model) || file.empty()) return WXL_E_INVALID;
        return binding::m2_replace_texture(model, type, file.c_str()) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    wxl_result WXL_CALL AttachmentPoint(void* model, uint32_t attachment, wxl_vec3* out)
    {
        if (!model || !out) return WXL_E_INVALID;
        return binding::m2_attachment_point(model, attachment, out) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    wxl_result WXL_CALL AttachToBone(void* model, void* parent, uint32_t attachment, uint32_t key_bone,
                                     const wxl_vec3* offset)
    {
        Model* known = Known(model);
        if (!known || !parent || parent == model || !offset) return WXL_E_INVALID;
        if (!binding::m2_has_key_bone(parent, key_bone)) return WXL_E_NOT_FOUND;
        if (!binding::m2_attach_model(model, parent, attachment, WXL_TRUE)) return WXL_E_NOT_FOUND;
        wxl::core::services::m2::Pose pose{};
        pose.kind    = wxl::core::services::m2::Pose::OnBone;
        pose.keyBone = key_bone;
        pose.offset  = *offset;
        SetPose(*known, pose);
        return WXL_OK;
    }

    // --- Models of the world --------------------------------------------------------------------

    wxl_m2_placement Placement(const wxl_m2_placement* in)
    {
        wxl_m2_placement out{};
        out.struct_size = sizeof out;
        out.scale       = 1.0f;
        if (!in) return out;
        const uint32_t size = in->struct_size ? in->struct_size : sizeof out;
        if (size >= offsetof(wxl_m2_placement, position) + sizeof(wxl_vec3)) out.position = in->position;
        if (size >= offsetof(wxl_m2_placement, roll) + sizeof(float))
        {
            out.yaw   = in->yaw;
            out.pitch = in->pitch;
            out.roll  = in->roll;
        }
        if (size >= offsetof(wxl_m2_placement, scale) + sizeof(float) && in->scale > 0.0f) out.scale = in->scale;
        return out;
    }

    void* WXL_CALL CreateWorldModel(wxl_str path)
    {
        const std::string file = Text(path);
        if (file.empty()) return nullptr;
        void* model = binding::m2_create_world_model(file.c_str());
        if (model) g_models[model] = Model{CurrentMod(), false, binding::m2_model_file(model)};
        return model;
    }

    wxl_result WXL_CALL PlaceModel(void* model, const wxl_m2_placement* placement)
    {
        Model* known = Known(model);
        if (!known || !placement) return WXL_E_INVALID;
        SetFollow(*known, false);
        const wxl_m2_placement at = Placement(placement);
        if (!binding::m2_place_world(model, &at)) return WXL_E_FAILED;
        known->world = true;
        return WXL_OK;
    }

    wxl_result WXL_CALL SetModelTransform(void* model, const wxl_m2_placement* placement)
    {
        if (!Known(model) || !placement) return WXL_E_INVALID;
        const wxl_m2_placement at = Placement(placement);
        binding::m2_set_transform(model, &at);
        return WXL_OK;
    }

    wxl_result WXL_CALL PlayAnimation(void* model, uint32_t anim_id, float speed)
    {
        if (!Known(model)) return WXL_E_INVALID;
        binding::m2_play_animation(model, anim_id, speed > 0.0f ? speed : 1.0f);
        return WXL_OK;
    }

    wxl_bool WXL_CALL ModelLoaded(void* model)
    {
        return model ? binding::m2_loaded(model) : WXL_FALSE;
    }

    wxl_result WXL_CALL AnimationLength(void* model, uint32_t anim_id, uint32_t* out_ms)
    {
        if (!model || !out_ms) return WXL_E_INVALID;
        if (!binding::m2_loaded(model)) return WXL_E_FAILED;
        *out_ms = binding::m2_animation_length(model, anim_id);
        return WXL_OK;
    }

    void WXL_CALL SetAlpha(void* model, float alpha)
    {
        if (Known(model)) binding::m2_set_alpha(model, alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha);
    }

    void WXL_CALL SetEmitting(void* model, wxl_bool emitting)
    {
        if (Known(model)) binding::m2_set_emitting(model, emitting);
    }

    void* WXL_CALL ObjectModel(wxl_handle object)
    {
        if (object == WXL_HANDLE_NONE) return nullptr;
        void* client = wxl::core::objects::Service().Handles().Resolve(object);
        return client ? binding::m2_object_model(client) : nullptr;
    }

    wxl_result WXL_CALL AttachmentWorld(void* model, uint32_t attachment, wxl_vec3* out)
    {
        if (!model || !out) return WXL_E_INVALID;
        if (!binding::m2_loaded(model)) return WXL_E_FAILED;
        return binding::m2_attachment_world(model, attachment, out) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    wxl_result WXL_CALL GroundHeight(const wxl_vec3* at, float up, float down, float* out_z)
    {
        if (!at || !out_z || up < 0.0f || down < 0.0f) return WXL_E_INVALID;
        return binding::m2_ground_height(at, up, down, out_z) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    const wxl_m2_api kApi = {
        sizeof(wxl_m2_api), &WatchEquipment, &UnwatchEquipment, &ItemDisplay,     &RacePrefix,
        &CreateModel,       &ReleaseModel,   &AttachModel,      &DetachModel,     &ModelParent,
        &SetGeosets,        &ReplaceTexture, &AttachmentPoint,  &AttachToBone,
        &CreateWorldModel,  &PlaceModel,     &SetModelTransform, &PlayAnimation,  &ModelLoaded,
        &AnimationLength,   &SetAlpha,       &SetEmitting,      &ObjectModel,     &AttachmentWorld,
        &GroundHeight,      &WatchBones,     &UnwatchBones,     &BoneInfo,
    };
}

namespace wxl::core::services
{
    const wxl_m2_api& M2Api() { return kApi; }

    namespace m2
    {
        bool Watching() { return g_live != 0; }

        void DispatchEquipment(const wxl_m2_equipment& change)
        {
            wxl::host::ModHost* host = wxl::core::Host();
            ++g_depth;
            // By index, and up to the size at the start: a watcher may watch again.
            const size_t count = g_watchers.size();
            for (size_t i = 0; i < count; ++i)
            {
                if (!g_watchers[i].live) continue;
                const Watcher    watcher  = g_watchers[i];
                WatcherCall      call{&watcher, &change};
                const uint32_t   previous = host ? host->SwapCurrentMod(watcher.owner) : 0;
                wxl::host::Fault fault;
                const bool       returned = wxl::host::CallGuarded(&InvokeWatcher, &call, &fault);
                if (host) host->SwapCurrentMod(previous);
                if (returned) continue;

                char what[96];
                wxl::host::DescribeFault(fault, what, sizeof what);
                WLOG_ERROR(kTag, "an equipment watcher faulted: %s", what);
                if (host && watcher.owner != 0)
                    host->Disable(watcher.owner, "in its equipment watcher");
                else
                    Drop(g_watchers[i]);
            }
            --g_depth;
            Compact();
        }

        bool Following() { return g_following != 0; }

        bool WatchingBones() { return g_boneLive.load() != 0; }

        void DispatchBones(void* model)
        {
            wxl_m2_bones bones{};
            bones.struct_size = sizeof bones;
            if (!binding::m2_bones(model, &bones) || !bones.file) return;
            const uint32_t mask = WantsOf(bones);
            for (uint32_t i = 0; i < kBoneSlots; ++i)
            {
                if (!(mask & (1u << i))) continue;
                BoneWatcher& watcher = g_bones[i];
                if (!watcher.live.load()) continue;
                BoneCall call{&watcher, &bones, WXL_TRUE};
                GuardedBones(watcher, &InvokeBoneWatcher, call);
            }
        }

        bool HasModels() { return !g_models.empty(); }

        bool Owns(void* model) { return model && g_models.count(model) != 0; }

        bool PoseOf(void* model, Pose& out)
        {
            auto it = g_models.find(model);
            if (it == g_models.end())
            {
                // An interface copy of a character duplicates its attachments: the copy is posed
                // as the model it was made from, which stays alive as long as the copy does.
                void* source = binding::m2_model_source(model);
                if (source) it = g_models.find(source);
            }
            if (it != g_models.end())
            {
                out = it->second.pose;
                return it->second.follow;
            }
            // Once loaded, a copy forgets the model it was copied from; the file is what remains.
            void* file = binding::m2_model_file(model);
            auto  by   = file ? g_followFiles.find(file) : g_followFiles.end();
            if (by == g_followFiles.end()) return false;
            out = by->second.pose;
            return true;
        }

        bool Follows(void* model)
        {
            Pose pose{};
            return PoseOf(model, pose);
        }

        void* SourceOf(void* model)
        {
            if (!model) return nullptr;
            if (g_models.count(model)) return model;
            void* source = binding::m2_model_source(model);
            if (source && g_models.count(source)) return source;
            void* file = binding::m2_model_file(model);
            if (!file) return nullptr;
            for (const auto& [owned, record] : g_models)
                if (record.file == file) return owned;
            return nullptr;
        }

        void ForgetMod(uint32_t mod)
        {
            for (Watcher& watcher : g_watchers)
                if (watcher.owner == mod) Drop(watcher);
            Compact();
            for (BoneWatcher& watcher : g_bones)
                if (watcher.owner == mod) DropBones(watcher);
            for (auto it = g_models.begin(); it != g_models.end();)
            {
                auto next = std::next(it);
                if (it->second.owner == mod) Forget(it);
                it = next;
            }
        }

        void Clear()
        {
            for (Watcher& watcher : g_watchers) Drop(watcher);
            Compact();
            for (BoneWatcher& watcher : g_bones) DropBones(watcher);
            {
                std::unique_lock lock(g_wantLock);
                g_wants.clear();
            }
            // On the way out the client is going too: forget the models without touching them.
            g_models.clear();
            g_followFiles.clear();
            g_following = 0;
        }
    }
}
