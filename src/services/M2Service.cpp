// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The wxl.m2 service: the models objects draw with, and models mods make and hang on them.
//
// Every model a mod makes is kept here with the mod that made it, so a call can refuse a model it
// was never given, and a mod that is disabled leaves nothing hanging on a character. A model hung
// to follow its parent's bones is posed here after the client posed it: its bones take the
// parent's bones of the same key bone id or name (WXL_M2_ATTACH_FOLLOW_BONES), or all of them one
// key bone of the parent moved by an offset (attach_to_bone). The same code poses on both
// clients, from the skeletons the binding describes.

#include "services/M2Service.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/mod_host.hpp"

#include <ankerl/unordered_dense.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr char kTag[] = "m2";

    namespace binding = wxl::core::binding;

    /// How a model wxl.m2 hung is posed each frame, after the client posed it itself.
    struct Pose
    {
        enum Kind : uint32_t
        {
            None,   ///< as the client poses it
            Bones,  ///< each bone as the parent's bone it matches (ATTACH_FOLLOW_BONES)
            OnBone, ///< every bone as one key bone of the parent, moved by an offset (attach_to_bone)
        };
        Kind     kind    = None;
        uint32_t keyBone = 0;
        wxl_vec3 offset{};
    };

    struct Model
    {
        uint32_t owner;
        void*    file; ///< its CM2Shared: what its interface copies are known by once they loaded
        Pose     pose;
    };

    struct FilePose
    {
        uint32_t count; ///< how many posed models are made of the file
        Pose     pose;  ///< the last one's: copies take it
    };

    ankerl::unordered_dense::map<void*, Model>    g_models;
    ankerl::unordered_dense::map<void*, FilePose> g_followFiles;
    std::atomic<uint32_t>                         g_following{0};

    uint32_t CurrentMod()
    {
        wxl::host::ModHost* host = wxl::core::Host();
        return host ? host->CurrentMod() : 0;
    }

    /// One kind of refusal, said once.
    struct Said
    {
        std::atomic<bool> said{false};
    };

    /// Whether the call runs where the client's models live; says once why not.
    bool OnMainThread(const char* call, Said& said)
    {
        const wxl::host::ModHost* host = wxl::core::Host();
        if (!host || host->OnMainThread()) return true;
        if (!said.said.exchange(true))
            WLOG_WARN(kTag, "wxl.m2.%s was called off the main thread, where the client's models are not "
                            "the mod's to touch; it was refused",
                      call);
        return false;
    }

    Model* Known(void* model)
    {
        if (!model) return nullptr;
        auto it = g_models.find(model);
        return it == g_models.end() ? nullptr : &it->second;
    }

    /// Sets how a model is posed each frame; a pose of kind None leaves it as the client poses it.
    void SetPose(Model& model, const Pose& pose)
    {
        if (model.pose.kind != Pose::None)
        {
            g_following.fetch_sub(1, std::memory_order_relaxed);
            if (model.file)
            {
                auto it = g_followFiles.find(model.file);
                if (it != g_followFiles.end() && --it->second.count == 0) g_followFiles.erase(it);
            }
        }
        model.pose = pose;
        if (pose.kind == Pose::None) return;
        g_following.fetch_add(1, std::memory_order_relaxed);
        if (!model.file) return;
        FilePose& file = g_followFiles[model.file];
        ++file.count;
        file.pose = pose;
    }

    /// Detaches and lets go of one model this service made, and forgets it.
    void Forget(void* model)
    {
        auto it = g_models.find(model);
        if (it == g_models.end()) return;
        SetPose(it->second, Pose{});
        g_models.erase(it);
        binding::m2_detach_model(model);
        binding::m2_release_model(model);
    }

    std::string Text(wxl_str text) { return text.data ? std::string(text.data, text.size) : std::string(); }

    // --- Posing -------------------------------------------------------------------------------

    constexpr uint32_t kBoneKeyId  = 0x00; // int32_t: the key bone id, -1 for none
    constexpr uint32_t kBoneParent = 0x08; // int16_t: the parent bone's index, -1 for the root
    constexpr uint32_t kBoneName   = 0x0C; // uint32_t: the hash of the bone's name, 0 for none
    constexpr uint32_t kBonePivot  = 0x4C; // C3Vector: where it sits in the bind pose
    constexpr uint32_t kBoneSize   = 0x58;
    constexpr uint16_t kNone       = 0xFFFF;
    constexpr size_t   kMatrix     = 16; // floats in a C44Matrix

    template <class T>
    T BoneField(const wxl_core_m2_skeleton& skeleton, uint32_t bone, uint32_t offset)
    {
        T value;
        std::memcpy(&value, skeleton.bones + static_cast<size_t>(bone) * skeleton.bone_stride + offset, sizeof value);
        return value;
    }

    uint16_t KeyBone(const wxl_core_m2_skeleton& skeleton, uint32_t key)
    {
        if (key >= skeleton.key_bone_count) return kNone;
        const uint16_t index = skeleton.key_bones[key];
        return index < skeleton.bone_count ? index : kNone;
    }

    /// Which parent bone each child bone takes its pose from, for one pair of model files: by key
    /// bone id first, then by name; a bone matching neither takes the parent bone nearest its pivot
    /// among the descendants of its nearest matched ancestor's counterpart (a later expansion's
    /// piece names thigh, knee and shin bones by key bones a 3.3.5a body lacks, which the body has
    /// under other ids at the same place). Each animate thread keeps the last few pairs it built;
    /// a pair is rebuilt when either file's bone count no longer agrees, which is all a recycled
    /// file can be told by.
    struct BoneMap
    {
        const void*           child       = nullptr;
        const void*           parent      = nullptr;
        uint32_t              childCount  = 0;
        uint32_t              parentCount = 0;
        bool                  matched     = false;
        std::vector<uint16_t> parentOf;
    };

    const BoneMap& MapBones(const wxl_core_m2_skeleton& child, const wxl_core_m2_skeleton& parent)
    {
        thread_local BoneMap  s_maps[8];
        thread_local uint32_t s_next = 0;

        for (const BoneMap& map : s_maps)
            if (map.child == child.file && map.parent == parent.file && map.childCount == child.bone_count &&
                map.parentCount == parent.bone_count)
                return map;

        BoneMap& map    = s_maps[s_next++ % 8];
        map.child       = child.file;
        map.parent      = parent.file;
        map.childCount  = child.bone_count;
        map.parentCount = parent.bone_count;
        map.matched     = false;
        map.parentOf.assign(child.bone_count, kNone);

        for (uint32_t i = 0; i < child.bone_count; ++i)
        {
            uint16_t      found = kNone;
            const int32_t key   = BoneField<int32_t>(child, i, kBoneKeyId);
            if (key >= 0) found = KeyBone(parent, static_cast<uint32_t>(key));
            const uint32_t name = BoneField<uint32_t>(child, i, kBoneName);
            if (found == kNone && name)
                for (uint32_t j = 0; j < parent.bone_count; ++j)
                    if (BoneField<uint32_t>(parent, j, kBoneName) == name)
                    {
                        found = static_cast<uint16_t>(j);
                        break;
                    }

            if (found != kNone)
            {
                map.matched = true;
            }
            else
            {
                const int16_t  up       = BoneField<int16_t>(child, i, kBoneParent);
                const uint16_t ancestor = up >= 0 && static_cast<uint32_t>(up) < i ? map.parentOf[up] : kNone;
                wxl_vec3       a        = BoneField<wxl_vec3>(child, i, kBonePivot);
                float          best     = 3.4e38f;
                for (uint32_t j = 0; j < parent.bone_count; ++j)
                {
                    if (ancestor != kNone)
                    {
                        uint32_t walk = j;
                        for (uint32_t depth = 0; walk != ancestor && walk < parent.bone_count && depth < parent.bone_count;
                             ++depth)
                        {
                            const int16_t next = BoneField<int16_t>(parent, walk, kBoneParent);
                            walk               = next < 0 ? kNone : static_cast<uint32_t>(next);
                        }
                        if (walk != ancestor) continue;
                    }
                    const wxl_vec3 b = BoneField<wxl_vec3>(parent, j, kBonePivot);
                    const float    d = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
                    if (d < best)
                    {
                        best  = d;
                        found = static_cast<uint16_t>(j);
                    }
                }
                if (found == kNone) found = ancestor;
            }
            map.parentOf[i] = found;
        }
        return map;
    }

    bool Skeletons(void* model, wxl_core_m2_skeleton& child, wxl_core_m2_skeleton& parent)
    {
        void* to = binding::m2_model_parent(model);
        return to && binding::m2_skeleton(model, &child) && binding::m2_skeleton(to, &parent) && child.matrices &&
               parent.matrices && child.bone_stride >= kBoneSize && parent.bone_stride >= kBoneSize;
    }

    /// Both are in view space: the parent's posed body is the child's, bone for bone.
    void PoseFromParent(void* model)
    {
        wxl_core_m2_skeleton child{}, parent{};
        if (!Skeletons(model, child, parent)) return;
        const BoneMap& map = MapBones(child, parent);
        if (!map.matched) return;
        for (uint32_t i = 0; i < map.childCount; ++i)
            if (map.parentOf[i] != kNone)
                std::memcpy(child.matrices + i * kMatrix, parent.matrices + map.parentOf[i] * kMatrix,
                            kMatrix * sizeof(float));
    }

    /// What CM2Model::AnimateAttachmentsMT hands an attachment: its bone, moved to the point
    /// (C44Matrix::Translate: the translation row gains the offset through the matrix).
    void PoseOnBone(void* model, uint32_t keyBone, const wxl_vec3& offset)
    {
        wxl_core_m2_skeleton child{}, parent{};
        if (!Skeletons(model, child, parent)) return;
        const uint16_t bone = KeyBone(parent, keyBone);
        if (bone == kNone) return;
        float m[kMatrix];
        std::memcpy(m, parent.matrices + bone * kMatrix, sizeof m);
        m[12] += m[0] * offset.x + m[4] * offset.y + m[8] * offset.z;
        m[13] += m[1] * offset.x + m[5] * offset.y + m[9] * offset.z;
        m[14] += m[2] * offset.x + m[6] * offset.y + m[10] * offset.z;
        for (uint32_t i = 0; i < child.bone_count; ++i) std::memcpy(child.matrices + i * kMatrix, m, sizeof m);
    }

    /// How `model`, or the model it is an interface copy of, is posed.
    Pose PoseOf(void* model)
    {
        auto it = g_models.find(model);
        if (it == g_models.end())
        {
            // An interface copy of a character duplicates its attachments: the copy is posed as
            // the model it was made from, which stays alive as long as the copy does.
            if (void* source = binding::m2_model_source(model)) it = g_models.find(source);
        }
        if (it != g_models.end()) return it->second.pose;
        // Once loaded, a copy forgets the model it was copied from; the file is what remains.
        void* file = binding::m2_model_file(model);
        auto  by   = file ? g_followFiles.find(file) : g_followFiles.end();
        return by == g_followFiles.end() ? Pose{} : by->second.pose;
    }

    // --- The table ----------------------------------------------------------------------------

    void* WXL_CALL ObjectModel(wxl_handle object)
    {
        static Said said;
        if (object == WXL_HANDLE_NONE || !OnMainThread("ObjectModel", said)) return nullptr;
        void* native = wxl::core::objects::Service().Handles().Resolve(object);
        return native ? binding::m2_object_model(native) : nullptr;
    }

    wxl_result WXL_CALL ItemDisplay(uint32_t display_id, wxl_m2_item_display* out)
    {
        if (!out || display_id == 0 || out->struct_size < sizeof(uint32_t) * 2) return WXL_E_INVALID;
        wxl_m2_item_display row{};
        row.struct_size = sizeof row;
        if (!binding::m2_item_display(display_id, &row)) return WXL_E_NOT_FOUND;
        // Written no longer than the caller's struct says it is.
        const uint32_t size = out->struct_size < sizeof row ? out->struct_size : static_cast<uint32_t>(sizeof row);
        row.struct_size     = size;
        std::memcpy(out, &row, size);
        return WXL_OK;
    }

    wxl_result WXL_CALL RacePrefix(uint32_t race_id, wxl_str* out)
    {
        if (!out) return WXL_E_INVALID;
        const char* prefix = binding::m2_race_prefix(race_id);
        if (!prefix) return WXL_E_NOT_FOUND;
        out->data = prefix;
        out->size = static_cast<uint32_t>(std::strlen(prefix));
        return WXL_OK;
    }

    void* WXL_CALL CreateModel(void* scene_model, wxl_str path)
    {
        static Said said;
        const std::string file = Text(path);
        if (!scene_model || file.empty() || !OnMainThread("CreateModel", said)) return nullptr;
        void* model = binding::m2_create_model(scene_model, file.c_str());
        if (model) g_models[model] = Model{CurrentMod(), binding::m2_model_file(model), Pose{}};
        return model;
    }

    void WXL_CALL ReleaseModel(void* model)
    {
        static Said said;
        if (OnMainThread("Release", said)) Forget(model);
    }

    wxl_result WXL_CALL AttachModel(void* model, void* parent, uint32_t attachment, uint32_t flags)
    {
        static Said said;
        if (!OnMainThread("Attach", said)) return WXL_E_INVALID;
        Model* known = Known(model);
        if (!known || !parent || parent == model) return WXL_E_INVALID;
        const wxl_bool to_root = (flags & WXL_M2_ATTACH_ROOT) ? WXL_TRUE : WXL_FALSE;
        if (!binding::m2_attach_model(model, parent, attachment, to_root)) return WXL_E_NOT_FOUND;
        Pose pose{};
        pose.kind = (flags & WXL_M2_ATTACH_FOLLOW_BONES) ? Pose::Bones : Pose::None;
        SetPose(*known, pose);
        return WXL_OK;
    }

    void WXL_CALL DetachModel(void* model)
    {
        static Said said;
        Model*      known = OnMainThread("Detach", said) ? Known(model) : nullptr;
        if (!known) return;
        SetPose(*known, Pose{});
        binding::m2_detach_model(model);
    }

    void* WXL_CALL ModelParent(void* model) { return Known(model) ? binding::m2_model_parent(model) : nullptr; }

    void WXL_CALL SetGeosets(void* model, uint32_t first, uint32_t last, wxl_bool visible)
    {
        static Said said;
        if (first <= last && OnMainThread("SetGeosets", said) && Known(model))
            binding::m2_set_geosets(model, first, last, visible);
    }

    wxl_result WXL_CALL ReplaceTexture(void* model, uint32_t type, wxl_str path)
    {
        static Said       said;
        const std::string file = Text(path);
        if (!OnMainThread("ReplaceTexture", said) || !Known(model) || file.empty()) return WXL_E_INVALID;
        return binding::m2_replace_texture(model, type, file.c_str()) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    wxl_result WXL_CALL AttachmentPoint(const void* model, uint32_t attachment, wxl_vec3* out)
    {
        if (!model || !out) return WXL_E_INVALID;
        return binding::m2_attachment_point(model, attachment, out) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    wxl_result WXL_CALL AttachToBone(void* model, void* parent, uint32_t attachment, uint32_t key_bone, wxl_vec3 offset)
    {
        static Said said;
        if (!OnMainThread("AttachToBone", said)) return WXL_E_INVALID;
        Model* known = Known(model);
        if (!known || !parent || parent == model) return WXL_E_INVALID;
        // A parent that has not loaded cannot be asked; it is taken at its word.
        wxl_core_m2_skeleton skeleton{};
        if (binding::m2_skeleton(parent, &skeleton) && KeyBone(skeleton, key_bone) == kNone) return WXL_E_NOT_FOUND;
        if (!binding::m2_attach_model(model, parent, attachment, WXL_TRUE)) return WXL_E_NOT_FOUND;
        Pose pose{};
        pose.kind    = Pose::OnBone;
        pose.keyBone = key_bone;
        pose.offset  = offset;
        SetPose(*known, pose);
        return WXL_OK;
    }

    const wxl_m2_api kApi = {
        sizeof(wxl_m2_api), &ObjectModel,  &ItemDisplay,     &RacePrefix,      &CreateModel,
        &ReleaseModel,      &AttachModel,  &DetachModel,     &ModelParent,     &SetGeosets,
        &ReplaceTexture,    &AttachmentPoint, &AttachToBone,
    };
}

namespace wxl::core::services
{
    const wxl_m2_api& M2Api() { return kApi; }

    namespace m2
    {
        bool Following() { return g_following.load(std::memory_order_relaxed) != 0; }

        void PoseAttached(void* model)
        {
            if (!model || !Following()) return;
            const Pose pose = PoseOf(model);
            if (pose.kind == Pose::OnBone)
                PoseOnBone(model, pose.keyBone, pose.offset);
            else if (pose.kind == Pose::Bones)
                PoseFromParent(model);
        }

        bool HasModels() { return !g_models.empty(); }

        void* SourceOf(void* model)
        {
            if (!model) return nullptr;
            if (g_models.contains(model)) return model;
            void* source = binding::m2_model_source(model);
            if (source && g_models.contains(source)) return source;
            void* file = binding::m2_model_file(model);
            if (!file) return nullptr;
            for (const auto& [owned, record] : g_models)
                if (record.file == file) return owned;
            return nullptr;
        }

        void ForgetMod(uint32_t mod)
        {
            std::vector<void*> theirs;
            for (const auto& [model, record] : g_models)
                if (record.owner == mod) theirs.push_back(model);
            for (void* model : theirs) Forget(model);
        }

        void Clear()
        {
            g_models.clear();
            g_followFiles.clear();
            g_following.store(0, std::memory_order_relaxed);
        }
    }
}
