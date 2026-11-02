#pragma once

#include "Assets\PostProcess\PostProcessEffectAsset.h"
#include "Graphics\Validation\PostProcessEffectsValidation.h"

namespace Engine
{
    class DX12Renderer;

    /** @brief Validates custom effect compilation, immutable snapshots, Profile persistence, HDR/LDR rendering and blending. */
    bool runPostProcessCustomEffectValidation(DX12Renderer& renderer, const std::filesystem::path& directory,
        const PostProcessValidationRender& render, const PostProcessValidationVerifySaved& verifySaved,
        PostProcessEffectHandle& retainedEffect);
}
