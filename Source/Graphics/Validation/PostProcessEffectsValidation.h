#pragma once

#include "Graphics\PostProcess\PostProcessSettings.h"
#include "Graphics\Camera\CameraTypes.h"
#include <filesystem>
#include <functional>
#include <cstdint>
#include <string>

namespace Engine
{
    class DX12Renderer;
    struct DX12FrameCapture;
    /** @brief Isolated fixture variants for effect, AA, constant-field and camera-removal regressions. */
    enum class PostProcessValidationFixture { Panels, Diagonal, Uniform, NoCamera, Nothing };
    /** @brief Fixed procedural scene rendering callback. */
    using PostProcessValidationRender = std::function<bool(std::uint32_t, std::uint32_t,
        const CameraViewport&, const PostProcessCameraSnapshot&, PostProcessValidationFixture)>;
    /** @brief Shared baseline/effects DDS and PNG round-trip verifier. */
    using PostProcessValidationVerifySaved = std::function<bool(const DX12FrameCapture&,
        const std::filesystem::path&, const std::wstring&)>;
    /** @brief Validates effects against independent CPU images, measured AA smoothing and preserved scene HDR. */
    bool runPostProcessEffectsValidation(DX12Renderer& renderer, const std::filesystem::path& directory,
        const PostProcessValidationRender& render, const PostProcessValidationVerifySaved& verifySaved);
}
