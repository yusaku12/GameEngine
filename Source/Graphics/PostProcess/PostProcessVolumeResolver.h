#pragma once

#include "Graphics\PostProcess\PostProcessSettings.h"
#include "Core\Object\ObjectGUID.h"
#include <span>
#include <vector>

namespace Engine
{
    class Scene;
    /** @brief CPU-only collected Volume value snapshot used for stable ordering. */
    struct PostProcessVolumeLayer
    {
        float priority = 0;
        ObjectGUID objectGuid;
        ObjectGUID sceneGuid;
        float weight = 0;
        PostProcessSettings settings;
        std::vector<PostProcessEffectPassSnapshot> customEffects;
    };
    /**
     * @brief Collects active, enabled, matching-layer Volumes, sorts Priority/GUID, and blends explicit overrides.
     * @details Caller-owned scratch capacity is reused; no component pointers survive resolution.
     * Camera Profile/Overrides and global Camera Weight are applied by the caller afterwards.
     * @return True on success; invalid position/allocation failure logs and preserves destination.
     * @thread_safety Main thread only. Scenes must remain structurally stable while collecting.
     */
    bool resolvePostProcessVolumes(PostProcessCameraSnapshot& destination, const Vector3& position,
        std::uint32_t layerMask, std::span<const Scene* const> scenes,
        std::vector<PostProcessVolumeLayer>& scratch) noexcept;
}
