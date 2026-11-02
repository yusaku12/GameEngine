#pragma once

#include "Core\Object\ObjectGUID.h"
#include "Graphics\PostProcess\PostProcessSettings.h"
#include "Assets\PostProcess\PostProcessEffectAsset.h"
#include <string>
#include <vector>

namespace Engine
{
    /** @brief An ordered custom shader instance stored by a Profile. */
    struct PostProcessEffectInstance
    {
        PostProcessEffectHandle effect; //!< Generation-checked immutable asset handle.
        bool enabled = true; //!< Whether this instance contributes a pass.
        std::array<float, 16> parameters{}; //!< Authored constants; only asset-declared values are consumed.
        bool operator==(const PostProcessEffectInstance&) const = default;
    };

    /** @brief Immutable shared Profile asset. Identity survives saves and snapshot replacement. */
    struct PostProcessProfile
    {
        ObjectGUID guid; //!< Asset identity.
        std::string name = "PostEffect Profile"; //!< Display name.
        PostProcessSettings settings; //!< Authored override values, not resolved camera state.
        std::vector<PostProcessEffectInstance> customEffects; //!< Ordered user-authored fullscreen passes.
        bool operator==(const PostProcessProfile&) const = default;
    };

    /** @brief Generation-checked engine resource handle, never an owning raw pointer. */
    struct PostProcessProfileHandle
    {
        std::uint32_t index = UINT32_MAX; //!< Registry slot.
        std::uint32_t generation = 0; //!< Slot lifetime.
        /** @brief Whether this is a syntactically valid handle; manager lookup validates lifetime. */
        bool isValid() const noexcept { return index != UINT32_MAX && generation != 0; }
        bool operator==(const PostProcessProfileHandle&) const = default;
    };
}
