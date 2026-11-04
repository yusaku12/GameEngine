#include "Pch.h"
#include "Graphics\PostProcess\PostProcessSettings.h"
#include <cmath>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\MathFunction.h"

namespace Engine
{
    void blendPostProcessLayer(PostProcessCameraSnapshot& destination,
        const PostProcessSettings& layer, const float weight) noexcept
    {
        if (weight == 0)
            return;
        for (const auto& descriptor : POST_PROCESS_FLOATS)
        {
            const auto& parameter = layer.*descriptor.member;
            if (parameter.overrideState)
            {
                auto& value = (destination.settings.*descriptor.member).value;
                value = weight == 1 ? parameter.value : std::lerp(value, parameter.value, weight);
            }
        }
        for (const auto& descriptor : POST_PROCESS_COLORS)
        {
            const auto& parameter = layer.*descriptor.member;
            if (parameter.overrideState)
            {
                auto& value = (destination.settings.*descriptor.member).value;
                value = weight == 1 ? parameter.value : Vector3::Lerp(value, parameter.value, weight);
            }
        }
        if (layer.colorGradingLutAsset.overrideState)
        {
            auto& destinationAsset = destination.settings.colorGradingLutAsset.value;
            destinationAsset = layer.colorGradingLutAsset.value;
            destination.settings.colorGradingLutAsset.overrideState = false;
        }
        for (std::size_t index = 0; index < POST_PROCESS_SWITCHES.size(); ++index)
        {
            const auto& descriptor = POST_PROCESS_SWITCHES[index];
            const auto& parameter = layer.*descriptor.member;
            if (!parameter.overrideState)
                continue;
            auto& contribution = destination.effects.*POST_PROCESS_WEIGHT_MEMBERS[index];
            const float previous = (destination.settings.*descriptor.member).value ? contribution : 0;
            contribution = weight == 1 ? (parameter.value ? 1.0f : 0.0f)
                : std::lerp(previous, parameter.value ? 1.0f : 0.0f, weight);
            (destination.settings.*descriptor.member).value = contribution > 0;
        }
    }
    bool validatePostProcessSettings(const PostProcessSettings& settings)
    {
        for (const auto& descriptor : POST_PROCESS_FLOATS)
        {
            const float value = (settings.*descriptor.member).value;
            if (!std::isfinite(value) || value < descriptor.minimum || value > descriptor.maximum)
            {
                LOG_ERROR("[PostProcess] '{}' must be finite and in [{}, {}].",
                    descriptor.name, descriptor.minimum, descriptor.maximum);
                return false;
            }
        }
        for (const auto& descriptor : POST_PROCESS_COLORS)
        {
            const auto& value = (settings.*descriptor.member).value;
            for (const float channel : { value.x, value.y, value.z })
                if (!std::isfinite(channel) || channel < descriptor.minimum || channel > descriptor.maximum)
                {
                    LOG_ERROR("[PostProcess] '{}' channels must be finite and in [{}, {}].",
                        descriptor.name, descriptor.minimum, descriptor.maximum);
                    return false;
                }
        }
        if (settings.autoExposureMinEV.value > settings.autoExposureMaxEV.value)
        {
            LOG_ERROR("[PostProcess] Auto Exposure Minimum EV must not exceed Maximum EV.");
            return false;
        }
        if (settings.colorGradingLutEnabled.value && !settings.colorGradingLutAsset.value.isValid())
        {
            LOG_ERROR("[PostProcess] An enabled Color Grading LUT requires a valid asset reference.");
            return false;
        }
        return true;
    }

    PostProcessSettings resolvePostProcessSettings(const PostProcessSettings* profile,
        const PostProcessSettings& cameraOverrides) noexcept
    {
        PostProcessCameraSnapshot resolved;
        for (const auto* layer : { profile, &cameraOverrides })
        {
            if (layer == nullptr)
                continue;
            blendPostProcessLayer(resolved, *layer, 1);
        }
        return resolved.settings;
    }
}
