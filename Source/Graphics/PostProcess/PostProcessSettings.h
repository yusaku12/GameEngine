#pragma once

#include "Core\Math\Geometry.h"
#include "Core\Object\ObjectGUID.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine
{
    class DX12Shader;

    /** @brief Shader入力/出力の色域。Both domains use linear color values. */
    enum class PostProcessStage
    {
        BeforeToneMapping, //!< Linear HDR.
        AfterToneMapping,  //!< Linear LDR; sRGB encoding has not been applied.
    };

    /** @brief Immutable custom fullscreen pass copied into a Camera render snapshot. */
    struct PostProcessEffectPassSnapshot
    {
        std::shared_ptr<const DX12Shader> pixelShader; //!< Immutable compiled bytecode snapshot.
        PostProcessStage stage = PostProcessStage::BeforeToneMapping; //!< Linear input/output domain.
        std::array<float, 16> parameters{}; //!< b1 constants.
        std::uint32_t resolutionDivisor = 1; //!< Target scale divisor in [1,16].
        float contribution = 1; //!< Blend custom output over the preceding image in [0,1].
        bool operator==(const PostProcessEffectPassSnapshot&) const = default;
    };

    /** @brief Value and explicit override state; disabled overrides retain their authored value. */
    template<class T>
    struct PostProcessParameter
    {
        T value{}; //!< Authored value.
        bool overrideState = false; //!< Apply this value when resolving a profile/camera layer.
        bool operator==(const PostProcessParameter&) const = default;
    };

    /** @brief Serializable authoring settings for built-in effects. Volume/Profile/Camera layers use override flags. */
    struct PostProcessSettings
    {
        PostProcessParameter<bool> bloomEnabled; //!< Bloom switch.
        PostProcessParameter<float> bloomIntensity{ 0.0f }; //!< Additive intensity, 0-100.
        PostProcessParameter<float> bloomThreshold{ -1.0f }; //!< Scene-linear threshold; -1 includes all pixels.
        PostProcessParameter<float> bloomScatter{ 0.7f }; //!< Pyramid scatter, 0-1.
        PostProcessParameter<float> bloomSizeScale{ 1.0f }; //!< Standard Bloom blur radius multiplier, 0.1-10.
        PostProcessParameter<bool> exposureEnabled; //!< Fixed exposure switch.
        PostProcessParameter<float> exposureEV{ 0.0f }; //!< Stops relative to renderer exposure, -16 to 16.
        PostProcessParameter<bool> autoExposureEnabled; //!< Temporally adapted automatic exposure switch.
        PostProcessParameter<float> autoExposureKey{ 0.18f }; //!< Target log-average luminance.
        PostProcessParameter<float> autoExposureMinEV{ -8.0f }; //!< Minimum automatic exposure in stops.
        PostProcessParameter<float> autoExposureMaxEV{ 8.0f }; //!< Maximum automatic exposure in stops.
        PostProcessParameter<float> autoExposureCompensation{ 0.0f }; //!< Exposure compensation in stops.
        PostProcessParameter<float> autoExposureSpeedUp{ 3.0f }; //!< Exposure increase adaptation rate in inverse seconds.
        PostProcessParameter<float> autoExposureSpeedDown{ 1.0f }; //!< Exposure decrease adaptation rate in inverse seconds.
        PostProcessParameter<bool> colorAdjustmentsEnabled; //!< Color adjustment switch.
        PostProcessParameter<float> contrast{ 0.0f }; //!< Percent, -100 to 100.
        PostProcessParameter<float> saturation{ 0.0f }; //!< Percent, -100 to 100.
        PostProcessParameter<Vector3> colorFilter{ Vector3::One }; //!< Linear multiplier, each channel 0-16.
        PostProcessParameter<float> colorTemperature{ 6500.0f }; //!< White balance temperature in Kelvin.
        PostProcessParameter<float> colorTint{ 0.0f }; //!< Green/magenta white-balance adjustment, -1 to 1.
        PostProcessParameter<float> colorGamma{ 1.0f }; //!< Global linear gamma, 0.1-4.
        PostProcessParameter<float> shadowSaturation{ 1.0f }; //!< Shadow saturation multiplier, 0-2.
        PostProcessParameter<float> shadowContrast{ 1.0f }; //!< Shadow contrast multiplier, 0-2.
        PostProcessParameter<float> shadowGamma{ 1.0f }; //!< Shadow gamma, 0.1-4.
        PostProcessParameter<float> shadowGain{ 1.0f }; //!< Shadow gain, 0-4.
        PostProcessParameter<float> midtoneSaturation{ 1.0f }; //!< Midtone saturation multiplier, 0-2.
        PostProcessParameter<float> midtoneContrast{ 1.0f }; //!< Midtone contrast multiplier, 0-2.
        PostProcessParameter<float> midtoneGamma{ 1.0f }; //!< Midtone gamma, 0.1-4.
        PostProcessParameter<float> midtoneGain{ 1.0f }; //!< Midtone gain, 0-4.
        PostProcessParameter<float> highlightSaturation{ 1.0f }; //!< Highlight saturation multiplier, 0-2.
        PostProcessParameter<float> highlightContrast{ 1.0f }; //!< Highlight contrast multiplier, 0-2.
        PostProcessParameter<float> highlightGamma{ 1.0f }; //!< Highlight gamma, 0.1-4.
        PostProcessParameter<float> highlightGain{ 1.0f }; //!< Highlight gain, 0-4.
        PostProcessParameter<Vector3> colorOffset{ Vector3::Zero }; //!< Global linear RGB offset, -1 to 1.
        PostProcessParameter<Vector3> shadowColor{ Vector3::Zero }; //!< Shadow color-wheel offset, -1 to 1.
        PostProcessParameter<Vector3> midtoneColor{ Vector3::Zero }; //!< Midtone color-wheel offset, -1 to 1.
        PostProcessParameter<Vector3> highlightColor{ Vector3::Zero }; //!< Highlight color-wheel offset, -1 to 1.
        PostProcessParameter<bool> vignetteEnabled; //!< Vignette switch.
        PostProcessParameter<float> vignetteIntensity{ 0.0f }; //!< Strength, 0-1.
        PostProcessParameter<float> vignetteSmoothness{ 0.5f }; //!< Falloff, 0.01-1.
        PostProcessParameter<float> vignetteRoundness{ 1.0f }; //!< Shape, 0-1.
        PostProcessParameter<Vector3> vignetteColor{ Vector3::Zero }; //!< Linear color, each channel 0-1.
        PostProcessParameter<bool> chromaticAberrationEnabled; //!< Chromatic aberration switch.
        PostProcessParameter<float> chromaticAberrationIntensity{ 0.0f }; //!< Radial RGB separation, 0-5.
        PostProcessParameter<float> chromaticAberrationStartOffset{ 0.5f }; //!< Radial onset, 0-1.
        PostProcessParameter<bool> filmGrainEnabled; //!< Temporally varying film grain switch.
        PostProcessParameter<float> filmGrainIntensity{ 0.0f }; //!< Linear display grain strength, 0-1.
        PostProcessParameter<float> filmGrainResponse{ 0.8f }; //!< Shadow-weighted grain response, 0-1.
        PostProcessParameter<bool> fxaaEnabled; //!< FXAA switch.
        PostProcessParameter<float> fxaaSubpixel{ 0.75f }; //!< Subpixel blending, 0-1.
        PostProcessParameter<float> fxaaEdgeThreshold{ 0.166f }; //!< Relative edge threshold, 0.03125-0.5.
        PostProcessParameter<float> fxaaEdgeThresholdMin{ 0.0833f }; //!< Absolute edge threshold, 0-0.5.
        PostProcessParameter<bool> temporalAAEnabled; //!< Jittered temporal reconstruction switch.
        PostProcessParameter<float> temporalAAFeedback{ 0.9f }; //!< History contribution, 0-0.99.
        PostProcessParameter<bool> motionBlurEnabled; //!< Velocity-buffer motion blur switch.
        PostProcessParameter<float> motionBlurAmount{ 0.5f }; //!< Shutter contribution, 0-1.
        PostProcessParameter<float> motionBlurMaxVelocity{ 32.0f }; //!< Maximum blur radius in pixels, 0-64.
        PostProcessParameter<bool> depthOfFieldEnabled; //!< Depth of Field switch.
        PostProcessParameter<float> depthOfFieldFocusDistance{ 10.0f }; //!< Focus distance in view-space units.
        PostProcessParameter<float> depthOfFieldFocusRange{ 5.0f }; //!< In-focus distance range.
        PostProcessParameter<float> depthOfFieldMaxBlur{ 8.0f }; //!< Maximum blur radius in pixels.
        PostProcessParameter<bool> colorGradingLutEnabled; //!< Color grading LUT switch.
        PostProcessParameter<float> colorGradingLutIntensity{ 1.0f }; //!< LUT blend, 0-1.
        PostProcessParameter<ObjectGUID> colorGradingLutAsset; //!< GUID for a linear 2D-strip 3D LUT.
        bool operator==(const PostProcessSettings&) const = default;
    };

    /** @brief Shared validation/Inspector metadata for scalar settings. */
    struct PostProcessFloatDescriptor
    {
        const char* name; //!< Inspector label.
        PostProcessParameter<float> PostProcessSettings::* member; //!< Authored setting.
        float minimum; //!< Inclusive minimum.
        float maximum; //!< Inclusive maximum.
    };

    inline constexpr std::array POST_PROCESS_FLOATS{
        PostProcessFloatDescriptor{ "Bloom Intensity", &PostProcessSettings::bloomIntensity, 0, 100 },
        PostProcessFloatDescriptor{ "Bloom Threshold", &PostProcessSettings::bloomThreshold, -1, 64 },
        PostProcessFloatDescriptor{ "Bloom Scatter", &PostProcessSettings::bloomScatter, 0, 1 },
        PostProcessFloatDescriptor{ "Bloom Size Scale", &PostProcessSettings::bloomSizeScale, 0.1f, 10 },
        PostProcessFloatDescriptor{ "Exposure EV", &PostProcessSettings::exposureEV, -16, 16 },
        PostProcessFloatDescriptor{ "Auto Exposure Key", &PostProcessSettings::autoExposureKey, 0.01f, 1 },
        PostProcessFloatDescriptor{ "Auto Exposure Minimum EV", &PostProcessSettings::autoExposureMinEV, -16, 16 },
        PostProcessFloatDescriptor{ "Auto Exposure Maximum EV", &PostProcessSettings::autoExposureMaxEV, -16, 16 },
        PostProcessFloatDescriptor{ "Auto Exposure Compensation", &PostProcessSettings::autoExposureCompensation, -16, 16 },
        PostProcessFloatDescriptor{ "Auto Exposure Speed Up", &PostProcessSettings::autoExposureSpeedUp, 0, 20 },
        PostProcessFloatDescriptor{ "Auto Exposure Speed Down", &PostProcessSettings::autoExposureSpeedDown, 0, 20 },
        PostProcessFloatDescriptor{ "Contrast", &PostProcessSettings::contrast, -100, 100 },
        PostProcessFloatDescriptor{ "Saturation", &PostProcessSettings::saturation, -100, 100 },
        PostProcessFloatDescriptor{ "White Balance Temperature", &PostProcessSettings::colorTemperature, 1500, 15000 },
        PostProcessFloatDescriptor{ "White Balance Tint", &PostProcessSettings::colorTint, -1, 1 },
        PostProcessFloatDescriptor{ "Global Gamma", &PostProcessSettings::colorGamma, 0.1f, 4 },
        PostProcessFloatDescriptor{ "Shadows Saturation", &PostProcessSettings::shadowSaturation, 0, 2 },
        PostProcessFloatDescriptor{ "Shadows Contrast", &PostProcessSettings::shadowContrast, 0, 2 },
        PostProcessFloatDescriptor{ "Shadows Gamma", &PostProcessSettings::shadowGamma, 0.1f, 4 },
        PostProcessFloatDescriptor{ "Shadows Gain", &PostProcessSettings::shadowGain, 0, 4 },
        PostProcessFloatDescriptor{ "Midtones Saturation", &PostProcessSettings::midtoneSaturation, 0, 2 },
        PostProcessFloatDescriptor{ "Midtones Contrast", &PostProcessSettings::midtoneContrast, 0, 2 },
        PostProcessFloatDescriptor{ "Midtones Gamma", &PostProcessSettings::midtoneGamma, 0.1f, 4 },
        PostProcessFloatDescriptor{ "Midtones Gain", &PostProcessSettings::midtoneGain, 0, 4 },
        PostProcessFloatDescriptor{ "Highlights Saturation", &PostProcessSettings::highlightSaturation, 0, 2 },
        PostProcessFloatDescriptor{ "Highlights Contrast", &PostProcessSettings::highlightContrast, 0, 2 },
        PostProcessFloatDescriptor{ "Highlights Gamma", &PostProcessSettings::highlightGamma, 0.1f, 4 },
        PostProcessFloatDescriptor{ "Highlights Gain", &PostProcessSettings::highlightGain, 0, 4 },
        PostProcessFloatDescriptor{ "Vignette Intensity", &PostProcessSettings::vignetteIntensity, 0, 1 },
        PostProcessFloatDescriptor{ "Vignette Smoothness", &PostProcessSettings::vignetteSmoothness, 0.01f, 1 },
        PostProcessFloatDescriptor{ "Vignette Roundness", &PostProcessSettings::vignetteRoundness, 0, 1 },
        PostProcessFloatDescriptor{ "Chromatic Aberration Intensity", &PostProcessSettings::chromaticAberrationIntensity, 0, 5 },
        PostProcessFloatDescriptor{ "Chromatic Aberration Start Offset", &PostProcessSettings::chromaticAberrationStartOffset, 0, 1 },
        PostProcessFloatDescriptor{ "Film Grain Intensity", &PostProcessSettings::filmGrainIntensity, 0, 1 },
        PostProcessFloatDescriptor{ "Film Grain Response", &PostProcessSettings::filmGrainResponse, 0, 1 },
        PostProcessFloatDescriptor{ "FXAA Subpixel", &PostProcessSettings::fxaaSubpixel, 0, 1 },
        PostProcessFloatDescriptor{ "FXAA Edge Threshold", &PostProcessSettings::fxaaEdgeThreshold, 0.03125f, 0.5f },
        PostProcessFloatDescriptor{ "FXAA Minimum Threshold", &PostProcessSettings::fxaaEdgeThresholdMin, 0, 0.5f },
        PostProcessFloatDescriptor{ "Focus Distance", &PostProcessSettings::depthOfFieldFocusDistance, 0.1f, 10000 },
        PostProcessFloatDescriptor{ "Focus Range", &PostProcessSettings::depthOfFieldFocusRange, 0.01f, 1000 },
        PostProcessFloatDescriptor{ "Maximum Blur", &PostProcessSettings::depthOfFieldMaxBlur, 0, 16 },
        PostProcessFloatDescriptor{ "LUT Intensity", &PostProcessSettings::colorGradingLutIntensity, 0, 1 },
        PostProcessFloatDescriptor{ "TAA History Feedback", &PostProcessSettings::temporalAAFeedback, 0, 0.99f },
        PostProcessFloatDescriptor{ "Motion Blur Amount", &PostProcessSettings::motionBlurAmount, 0, 1 },
        PostProcessFloatDescriptor{ "Motion Blur Maximum Velocity", &PostProcessSettings::motionBlurMaxVelocity, 0, 64 },
    };

    /** @brief Shared metadata for effect switches. */
    struct PostProcessBoolDescriptor
    {
        const char* name; //!< Inspector label.
        PostProcessParameter<bool> PostProcessSettings::* member; //!< Authored switch.
    };
    inline constexpr std::array POST_PROCESS_SWITCHES{
        PostProcessBoolDescriptor{ "Bloom Enabled", &PostProcessSettings::bloomEnabled },
        PostProcessBoolDescriptor{ "Exposure Enabled", &PostProcessSettings::exposureEnabled },
        PostProcessBoolDescriptor{ "Color Grading Enabled", &PostProcessSettings::colorAdjustmentsEnabled },
        PostProcessBoolDescriptor{ "Vignette Enabled", &PostProcessSettings::vignetteEnabled },
        PostProcessBoolDescriptor{ "Chromatic Aberration Enabled", &PostProcessSettings::chromaticAberrationEnabled },
        PostProcessBoolDescriptor{ "Film Grain Enabled", &PostProcessSettings::filmGrainEnabled },
        PostProcessBoolDescriptor{ "FXAA Enabled", &PostProcessSettings::fxaaEnabled },
        PostProcessBoolDescriptor{ "Temporal Anti-Aliasing Enabled", &PostProcessSettings::temporalAAEnabled },
        PostProcessBoolDescriptor{ "Motion Blur Enabled", &PostProcessSettings::motionBlurEnabled },
        PostProcessBoolDescriptor{ "Depth of Field Enabled", &PostProcessSettings::depthOfFieldEnabled },
        PostProcessBoolDescriptor{ "Color Grading LUT Enabled", &PostProcessSettings::colorGradingLutEnabled },
        PostProcessBoolDescriptor{ "Auto Exposure Enabled", &PostProcessSettings::autoExposureEnabled },
    };
    /** @brief Continuous Enabled contributions; indices follow POST_PROCESS_SWITCHES. */
    struct PostProcessEffectWeights
    {
        float bloom = 1;
        float exposure = 1;
        float colorAdjustments = 1;
        float vignette = 1;
        float fxaa = 1;
        float chromaticAberration = 1;
        float filmGrain = 1;
        float depthOfField = 1;
        float colorGradingLut = 1;
        float autoExposure = 1;
        float temporalAA = 1;
        float motionBlur = 1;
        bool operator==(const PostProcessEffectWeights&) const = default;
    };

    /** @brief Directional-light shaft inputs resolved for the current camera. */
    struct PostProcessLightShaftSnapshot
    {
        bool bloomEnabled = false; //!< Radial HDR bloom switch.
        bool occlusionEnabled = false; //!< Mask samples hidden by scene depth.
        Vector2 sourcePosition = Vector2::Zero; //!< Normalized camera-viewport position; may be off-screen.
        Vector3 tint = Vector3::One; //!< Linear RGB tint.
        float bloomScale = 0.0f; //!< Additive shaft contribution.
        float bloomThreshold = 1.0f; //!< Scene-linear brightness threshold.
        float occlusionMaskDarkness = 0.5f; //!< Attenuation for samples covered by geometry.
        bool operator==(const PostProcessLightShaftSnapshot&) const = default;
    };

    inline constexpr std::array POST_PROCESS_WEIGHT_MEMBERS{
        &PostProcessEffectWeights::bloom, &PostProcessEffectWeights::exposure,
        &PostProcessEffectWeights::colorAdjustments, &PostProcessEffectWeights::vignette,
        &PostProcessEffectWeights::chromaticAberration, &PostProcessEffectWeights::filmGrain,
        &PostProcessEffectWeights::fxaa, &PostProcessEffectWeights::temporalAA,
        &PostProcessEffectWeights::motionBlur, &PostProcessEffectWeights::depthOfField,
        &PostProcessEffectWeights::colorGradingLut, &PostProcessEffectWeights::autoExposure,
    };

    /** @brief Shared metadata for linear RGB settings. */
    struct PostProcessColorDescriptor
    {
        const char* name; //!< Inspector label.
        PostProcessParameter<Vector3> PostProcessSettings::* member; //!< Authored color.
        float minimum; //!< Inclusive channel minimum.
        float maximum; //!< Inclusive channel maximum.
    };
    inline constexpr std::array POST_PROCESS_COLORS{
        PostProcessColorDescriptor{ "Color Filter (Linear)", &PostProcessSettings::colorFilter, 0, 16 },
        PostProcessColorDescriptor{ "Global Color Offset (Linear)", &PostProcessSettings::colorOffset, -1, 1 },
        PostProcessColorDescriptor{ "Shadows Color Wheel (Linear)", &PostProcessSettings::shadowColor, -1, 1 },
        PostProcessColorDescriptor{ "Midtones Color Wheel (Linear)", &PostProcessSettings::midtoneColor, -1, 1 },
        PostProcessColorDescriptor{ "Highlights Color Wheel (Linear)", &PostProcessSettings::highlightColor, -1, 1 },
        PostProcessColorDescriptor{ "Vignette Color (Linear)", &PostProcessSettings::vignetteColor, 0, 1 },
    };

    /** @brief Rejects nonfinite/out-of-range values, including inactive overrides, with a diagnostic. */
    bool validatePostProcessSettings(const PostProcessSettings& settings);
    /** @brief Resolves neutral defaults, then profile overrides, then camera overrides. No GPU work. */
    PostProcessSettings resolvePostProcessSettings(const PostProcessSettings* profile,
        const PostProcessSettings& cameraOverrides) noexcept;

    /** @brief CPU-only value snapshot sent to the render thread; never references mutable authoring data. */
    struct PostProcessCameraSnapshot
    {
        bool enabled = false; //!< Camera opt-in.
        float weight = 1.0f; //!< Contribution: neutral parameter blending, FXAA result blending, 0-1.
        ObjectGUID cameraGuid{}; //!< Stable identity used to isolate GPU temporal history.
        float deltaTimeSeconds = 0.0f; //!< Simulation time elapsed for temporal effect updates.
        float nearClip = 0.1f; //!< Camera near plane for depth-aware effects.
        float farClip = 1000.0f; //!< Camera far plane for depth-aware effects.
        bool perspectiveProjection = true; //!< Selects perspective vs. orthographic depth linearization.
        bool temporalHistoryReset = false; //!< Camera cut/projection reset invalidates temporal history.
        PostProcessSettings settings; //!< Resolved values with authored override flags removed.
        PostProcessEffectWeights effects; //!< Continuous switch contributions; default preserves pre-Volume snapshots.
        PostProcessLightShaftSnapshot lightShafts; //!< Active directional-light shaft snapshot.
        std::vector<PostProcessEffectPassSnapshot> customEffects; //!< Ordered immutable custom shader passes.
        bool operator==(const PostProcessCameraSnapshot&) const = default;
    };
    /**
     * @brief Applies only explicit overrides; numeric values and Enabled contributions interpolate independently.
     * @pre layer has been validated; weight is finite and in [0,1].
     */
    void blendPostProcessLayer(PostProcessCameraSnapshot& destination,
        const PostProcessSettings& layer, float weight) noexcept;
}
