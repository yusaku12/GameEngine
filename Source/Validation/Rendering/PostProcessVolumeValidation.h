#pragma once

#include "Assets\PostProcess\PostProcessEffectAsset.h"
#include "Validation\Rendering\PostProcessEffectsValidation.h"

namespace Engine
{
    /** @brief Validates Volume geometry, blend order, persistence, Inspector and actual Camera GPU snapshots. */
    bool runPostProcessVolumeValidation(DX12Renderer& renderer, const std::filesystem::path& directory,
        const PostProcessValidationRender& render, const PostProcessValidationVerifySaved& verifySaved,
        PostProcessEffectHandle customEffect);
}
