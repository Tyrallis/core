// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The asset pipeline: the sources mods add to the client's files (providers and served bytes),
// the transforms that rewrite a kind of file between its read and its parse, and the file layer
// through which the client reads both, the same on both builds.

#pragma once

#include "wxl/services/storage.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace wxl::core::services::assets
{
    // --- what the FindFile gate asks ----------------------------------------------------------

    /// Whether any source or transform is registered: one load each, the gate's fast path.
    bool Active();

    /// What a thread is doing inside the runtime's own resolution, for the gates to know.
    enum class Mode
    {
        Normal,     ///< the client, or a mod through wxl.storage
        ClientOnly, ///< asking whether the client itself has a name: no source answers
        Inner,      ///< reading a file to transform it: sources answer, transforms and events do not
    };

    /// The calling thread's mode.
    Mode Current();

    /// Sets the calling thread's mode for the life of the object.
    class ModeScope
    {
    public:
        explicit ModeScope(Mode mode);
        ~ModeScope();
        ModeScope(const ModeScope&)            = delete;
        ModeScope& operator=(const ModeScope&) = delete;

    private:
        Mode previous_;
    };

    /// A source that holds a name: its id, for the path the file layer opens, and its priority.
    struct Candidate
    {
        uint32_t id       = 0;
        int32_t  priority = 0;
    };

    /**
     * @brief The highest source above @p floor that holds @p name: a provider asked by
     *        priority, or bytes a mod serves. False when none does. A provider that answers
     *        WXL_E_PENDING is passed over and noted for PendingSeen.
     */
    bool BestSource(const char* name, int32_t floor, Candidate* out);

    /// Forgets whether a provider answered "not yet" on this thread; PendingSeen then says
    /// whether one did since, so that wxl.storage tells a mod WXL_E_PENDING rather than
    /// WXL_E_NOT_FOUND.
    void ClearPending();
    bool PendingSeen();

    /// Whether some transform is registered for the kind @p name is, by its extension.
    bool Transformed(const char* name);

    /**
     * @brief Writes the path the file layer opens a source's file by, "|wxl|<id>|<name>" (id 0
     *        for a file opened to be transformed), and makes sure the layer is in the client's
     *        stack. False when the path does not fit or the client's stack is not built yet.
     */
    bool LayerPath(uint32_t id, const char* name, char* path, size_t capacity);

    /// Whether a path is one LayerPath wrote, and the client's name inside it.
    bool IsLayerPath(const char* path, uint32_t* id, const char** name);

    // --- the members of wxl.storage -----------------------------------------------------------

    wxl_result AddProvider(const wxl_asset_provider* provider, int32_t priority, uint64_t* out, uint32_t mod);
    wxl_result RemoveProvider(uint64_t handle, uint32_t mod);
    wxl_result AddTransform(const wxl_asset_transform* transform, int32_t priority, uint64_t* out, uint32_t mod);
    wxl_result RemoveTransform(uint64_t handle, uint32_t mod);
    wxl_result Serve(wxl_str path, const void* data, uint32_t size, int32_t priority, uint32_t mod);
    wxl_result Unserve(wxl_str path, uint32_t mod);

    /// Tells @p keep every name the providers list and the mods serve that matches @p pattern
    /// (`*` any run of characters, case and slashes aside).
    void ListMatching(const char* pattern, const std::function<void(const std::string&)>& keep,
                      const std::function<bool(const char*, const char*)>& matches);

    /// Takes back everything a mod registered: it is disabled or unloaded. Waits for the calls
    /// in progress and closes the files the client holds open from its providers.
    void ReleaseOf(uint32_t mod);

    /// Takes back everything, for the harness and the way out.
    void ReleaseAll();

    // --- the file layer -----------------------------------------------------------------------

    /// Puts the layer's verbs into the client's top filestack node, once; true when they are.
    bool Install();

    /// Forgets the node the layer was put into, for the harness, which builds a new one.
    void ForgetInstall();
}
