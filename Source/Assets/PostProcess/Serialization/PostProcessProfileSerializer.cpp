#include "Pch.h"
#include "Assets\PostProcess\PostProcessProfileManager.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Assets\PostProcess\PostProcessOcioAsset.h"
#include "Core\Containers\String.h"
#include "Assets\PostProcess\Serialization\PostProcessProfileSerializer.h"
#include "Assets\PostProcess\Serialization\PostProcessEffectSerializer.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\PostProcessProfile_generated.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\MathFunction.h"

namespace Engine::Serialization
{
    namespace
    {
        bool validateEffectInstances(const std::vector<PostProcessEffectInstance>& effects)
        {
            auto& manager = PostProcessEffectAssetManager::instance();
            for (const auto& instance : effects)
            {
                const auto asset = manager.get(instance.effect);
                if (!asset || asset->parameters.size() > instance.parameters.size())
                {
                    LOG_ERROR("[PostProcess] Profile references a stale custom Effect asset.");
                    return false;
                }
                for (std::size_t index = 0; index < asset->parameters.size(); ++index)
                {
                    const float value = instance.parameters[index];
                    const auto& definition = asset->parameters[index];
                    if (!std::isfinite(value) || value < definition.minimum || value > definition.maximum)
                    {
                        LOG_ERROR("[PostProcess] Effect '{}' parameter '{}' is outside [{}, {}].",
                            asset->name, definition.name, definition.minimum, definition.maximum);
                        return false;
                    }
                }
            }
            return true;
        }
    }

    bool writePostProcessProfileReference(const PostProcessProfileHandle handle, PostProcessProfileReference& destination)
    {
        PostProcessProfileReference candidate;
        if (handle.isValid())
        {
            auto& manager = PostProcessProfileManager::instance();
            const auto profile = manager.get(handle);
            const auto path = manager.getPath(handle);
            if (!profile || path.empty())
            {
                LOG_ERROR("[PostProcess] Save the referenced Profile before saving its Scene/Prefab.");
                return false;
            }
            const auto text = String::fromWide(path.c_str());
            if (text.size() == 0)
            {
                LOG_ERROR("[PostProcess] Cannot encode Profile path as UTF-8.");
                return false;
            }
            candidate = { profile->guid, std::string(text.c_str(), text.size()) };
        }
        destination = std::move(candidate);
        return true;
    }

    bool readPostProcessProfileReference(const PostProcessProfileReference& reference, PostProcessProfileHandle& destination)
    {
        if (reference.path.find('\0') != std::string::npos || reference.guid.isValid() != !reference.path.empty())
        {
            LOG_ERROR("[PostProcess] Invalid Profile GUID/path pair.");
            return false;
        }
        PostProcessProfileHandle candidate;
        if (reference.guid.isValid())
        {
            const auto wide = String(reference.path).toWide();
            if (wide.empty())
            {
                LOG_ERROR("[PostProcess] Invalid UTF-8 Profile path.");
                return false;
            }
            candidate = PostProcessProfileManager::instance().load(std::filesystem::path(wide), reference.guid);
            if (!candidate.isValid())
                return false;
        }
        destination = candidate;
        return true;
    }
    namespace
    {
        PostProcessFloat toFlat(const PostProcessParameter<float>& parameter)
        {
            return { parameter.value, parameter.overrideState };
        }
        PostProcessBool toFlat(const PostProcessParameter<bool>& parameter)
        {
            return { parameter.value, parameter.overrideState };
        }
        PostProcessColor toFlat(const PostProcessParameter<Vector3>& parameter)
        {
            return { Vec3(parameter.value.x, parameter.value.y, parameter.value.z), parameter.overrideState };
        }
        bool read(const PostProcessFloat* source, PostProcessParameter<float>& destination)
        {
            if (source == nullptr) return false;
            destination = { source->value(), source->overridden() };
            return true;
        }
        bool read(const PostProcessBool* source, PostProcessParameter<bool>& destination)
        {
            if (source == nullptr) return false;
            destination = { source->value(), source->overridden() };
            return true;
        }
        bool read(const PostProcessColor* source, PostProcessParameter<Vector3>& destination)
        {
            if (source == nullptr) return false;
            const auto& value = source->value();
            destination = { Vector3(value.x(), value.y(), value.z()), source->overridden() };
            return true;
        }
    }

    flatbuffers::Offset<PostProcessSettingsData> writePostProcessSettings(
        flatbuffers::FlatBufferBuilder& builder, const Engine::PostProcessSettings& settings)
    {
        const auto bloomEnabled = toFlat(settings.bloomEnabled);
        const auto bloomIntensity = toFlat(settings.bloomIntensity);
        const auto bloomThreshold = toFlat(settings.bloomThreshold);
        const auto bloomScatter = toFlat(settings.bloomScatter);
        const auto bloomSizeScale = toFlat(settings.bloomSizeScale);
        const auto exposureEnabled = toFlat(settings.exposureEnabled);
        const auto exposureEV = toFlat(settings.exposureEV);
        const auto vignetteEnabled = toFlat(settings.vignetteEnabled);
        const auto vignetteIntensity = toFlat(settings.vignetteIntensity);
        const auto vignetteSmoothness = toFlat(settings.vignetteSmoothness);
        const auto vignetteRoundness = toFlat(settings.vignetteRoundness);
        const auto vignetteColor = toFlat(settings.vignetteColor);
        const auto fxaaEnabled = toFlat(settings.fxaaEnabled);
        const auto fxaaSubpixel = toFlat(settings.fxaaSubpixel);
        const auto fxaaThreshold = toFlat(settings.fxaaEdgeThreshold);
        const auto fxaaMinimum = toFlat(settings.fxaaEdgeThresholdMin);
        const auto temporalAAEnabled = toFlat(settings.temporalAAEnabled);
        const auto temporalAAFeedback = toFlat(settings.temporalAAFeedback);
        const auto motionBlurEnabled = toFlat(settings.motionBlurEnabled);
        const auto motionBlurAmount = toFlat(settings.motionBlurAmount);
        const auto motionBlurMaxVelocity = toFlat(settings.motionBlurMaxVelocity);
        const auto depthOfFieldEnabled = toFlat(settings.depthOfFieldEnabled);
        const auto depthOfFieldFocusDistance = toFlat(settings.depthOfFieldFocusDistance);
        const auto depthOfFieldFocusRange = toFlat(settings.depthOfFieldFocusRange);
        const auto depthOfFieldMaxBlur = toFlat(settings.depthOfFieldMaxBlur);
        const auto legacyFogEnabled = toFlat(PostProcessParameter<bool>{});
        const auto legacyFogDensity = toFlat(PostProcessParameter<float>{});
        const auto legacyFogStartDistance = toFlat(PostProcessParameter<float>{});
        const auto legacyFogColor = toFlat(PostProcessParameter<Vector3>{});
        const auto autoExposureEnabled = toFlat(settings.autoExposureEnabled);
        const auto autoExposureKey = toFlat(settings.autoExposureKey);
        const auto autoExposureMinEV = toFlat(settings.autoExposureMinEV);
        const auto autoExposureMaxEV = toFlat(settings.autoExposureMaxEV);
        const auto autoExposureCompensation = toFlat(settings.autoExposureCompensation);
        const auto autoExposureSpeedUp = toFlat(settings.autoExposureSpeedUp);
        const auto autoExposureSpeedDown = toFlat(settings.autoExposureSpeedDown);
        const auto chromaticAberrationEnabled = toFlat(settings.chromaticAberrationEnabled);
        const auto chromaticAberrationIntensity = toFlat(settings.chromaticAberrationIntensity);
        const auto chromaticAberrationStartOffset = toFlat(settings.chromaticAberrationStartOffset);
        const auto filmGrainEnabled = toFlat(settings.filmGrainEnabled);
        const auto filmGrainIntensity = toFlat(settings.filmGrainIntensity);
        const auto filmGrainResponse = toFlat(settings.filmGrainResponse);
        const auto ocioEnabled = toFlat(settings.ocioEnabled);
        const auto ocioIntensity = toFlat(settings.ocioIntensity);
        std::string ocioPath;
        if (settings.ocioAsset.value)
        {
            const auto& path = settings.ocioAsset.value->assetPath;
            const auto text = String::fromWide(path.c_str());
            if (path.empty() || text.size() == 0)
            {
                LOG_ERROR("[OCIO] Save the compiled asset before saving PostProcess settings.");
                return {};
            }
            ocioPath.assign(text.c_str(), text.size());
        }
        return CreatePostProcessSettingsData(builder, &bloomEnabled, &bloomIntensity,
            &bloomThreshold, &bloomScatter, &exposureEnabled, &exposureEV, &vignetteEnabled, &vignetteIntensity,
            &vignetteSmoothness, &vignetteRoundness, &vignetteColor, &fxaaEnabled,
            &fxaaSubpixel, &fxaaThreshold, &fxaaMinimum, &depthOfFieldEnabled,
            &depthOfFieldFocusDistance, &depthOfFieldFocusRange, &depthOfFieldMaxBlur,
            &legacyFogEnabled, &legacyFogDensity, &legacyFogStartDistance, &legacyFogColor,
            &autoExposureEnabled, &autoExposureKey,
            &autoExposureMinEV, &autoExposureMaxEV, &autoExposureCompensation,
            &autoExposureSpeedUp, &autoExposureSpeedDown, &bloomSizeScale,
            &chromaticAberrationEnabled, &chromaticAberrationIntensity, &chromaticAberrationStartOffset,
            &filmGrainEnabled, &filmGrainIntensity, &filmGrainResponse, &temporalAAEnabled,
            &temporalAAFeedback, &motionBlurEnabled, &motionBlurAmount, &motionBlurMaxVelocity,
            &ocioEnabled, &ocioIntensity, builder.CreateString(ocioPath), settings.ocioAsset.overrideState);
    }

    bool readPostProcessSettings(const PostProcessSettingsData* source, Engine::PostProcessSettings& settings)
    {
        Engine::PostProcessSettings loaded;
        if (source == nullptr
            || !read(source->bloom_enabled(), loaded.bloomEnabled)
            || !read(source->bloom_intensity(), loaded.bloomIntensity)
            || !read(source->bloom_threshold(), loaded.bloomThreshold)
            || !read(source->bloom_scatter(), loaded.bloomScatter)
            || !read(source->exposure_enabled(), loaded.exposureEnabled)
            || !read(source->exposure_ev(), loaded.exposureEV)
            || !read(source->vignette_enabled(), loaded.vignetteEnabled)
            || !read(source->vignette_intensity(), loaded.vignetteIntensity)
            || !read(source->vignette_smoothness(), loaded.vignetteSmoothness)
            || !read(source->vignette_roundness(), loaded.vignetteRoundness)
            || !read(source->vignette_color(), loaded.vignetteColor)
            || !read(source->fxaa_enabled(), loaded.fxaaEnabled)
            || !read(source->fxaa_subpixel(), loaded.fxaaSubpixel)
            || !read(source->fxaa_edge_threshold(), loaded.fxaaEdgeThreshold)
            || !read(source->fxaa_edge_threshold_min(), loaded.fxaaEdgeThresholdMin))
        {
            LOG_ERROR("[PostProcess] Serialized settings are missing required parameters.");
            return false;
        }
        const auto readOptional = [](const auto* serialized, auto& destination)
        {
            return serialized == nullptr || read(serialized, destination);
        };
        if (!readOptional(source->depth_of_field_enabled(), loaded.depthOfFieldEnabled)
            || !readOptional(source->depth_of_field_focus_distance(), loaded.depthOfFieldFocusDistance)
            || !readOptional(source->depth_of_field_focus_range(), loaded.depthOfFieldFocusRange)
            || !readOptional(source->depth_of_field_max_blur(), loaded.depthOfFieldMaxBlur)
            || !readOptional(source->auto_exposure_enabled(), loaded.autoExposureEnabled)
            || !readOptional(source->auto_exposure_key(), loaded.autoExposureKey)
            || !readOptional(source->auto_exposure_min_ev(), loaded.autoExposureMinEV)
            || !readOptional(source->auto_exposure_max_ev(), loaded.autoExposureMaxEV)
            || !readOptional(source->auto_exposure_compensation(), loaded.autoExposureCompensation)
            || !readOptional(source->auto_exposure_speed_up(), loaded.autoExposureSpeedUp)
            || !readOptional(source->auto_exposure_speed_down(), loaded.autoExposureSpeedDown)
            || !readOptional(source->bloom_size_scale(), loaded.bloomSizeScale)
            || !readOptional(source->chromatic_aberration_enabled(), loaded.chromaticAberrationEnabled)
            || !readOptional(source->chromatic_aberration_intensity(), loaded.chromaticAberrationIntensity)
            || !readOptional(source->chromatic_aberration_start_offset(), loaded.chromaticAberrationStartOffset)
            || !readOptional(source->film_grain_enabled(), loaded.filmGrainEnabled)
            || !readOptional(source->film_grain_intensity(), loaded.filmGrainIntensity)
            || !readOptional(source->film_grain_response(), loaded.filmGrainResponse)
            || !readOptional(source->temporal_aa_enabled(), loaded.temporalAAEnabled)
            || !readOptional(source->temporal_aa_feedback(), loaded.temporalAAFeedback)
            || !readOptional(source->motion_blur_enabled(), loaded.motionBlurEnabled)
            || !readOptional(source->motion_blur_amount(), loaded.motionBlurAmount)
            || !readOptional(source->motion_blur_max_velocity(), loaded.motionBlurMaxVelocity)
            || !readOptional(source->ocio_enabled(), loaded.ocioEnabled)
            || !readOptional(source->ocio_intensity(), loaded.ocioIntensity))
        {
            LOG_ERROR("[PostProcess] Serialized Phase 6 settings are invalid.");
            return false;
        }
        if (const auto* path = source->ocio_asset_path())
        {
            const std::string text = path->str();
            if (text.find('\0') != std::string::npos)
            {
                LOG_ERROR("[OCIO] Compiled asset path contains a null character.");
                return false;
            }
            loaded.ocioAsset.overrideState = source->ocio_asset_overridden();
            if (!text.empty())
            {
                const auto wide = String(text).toWide();
                if (wide.empty())
                {
                    LOG_ERROR("[OCIO] Invalid compiled asset path encoding.");
                    return false;
                }
                auto asset = std::make_shared<PostProcessOcioAsset>();
                if (!loadPostProcessOcioAsset(std::filesystem::path(wide), *asset))
                    return false;
                loaded.ocioAsset.value = std::move(asset);
            }
        }
        if (!validatePostProcessSettings(loaded))
            return false;
        settings = loaded;
        return true;
    }

    bool PostProcessProfileSerializer::save(const std::filesystem::path& path,
        const Engine::PostProcessProfile& profile) const
    {
        if (!profile.guid.isValid() || profile.name.empty() || profile.name.find('\0') != std::string::npos)
        {
            LOG_ERROR("[PostProcess] Cannot save an invalid Profile GUID/name.");
            return false;
        }
        if (!validatePostProcessSettings(profile.settings) || !validateEffectInstances(profile.customEffects))
            return false;
        flatbuffers::FlatBufferBuilder builder(512);
        const AssetGuid guid(profile.guid.high, profile.guid.low);
        const auto settings = writePostProcessSettings(builder, profile.settings);
        if (settings.IsNull())
            return false;
        std::vector<flatbuffers::Offset<PostProcessEffectInstanceData>> effects;
        effects.reserve(profile.customEffects.size());
        for (const auto& instance : profile.customEffects)
        {
            PostProcessEffectReference reference;
            if (!writePostProcessEffectReference(instance.effect, reference))
                return false;
            const auto asset = PostProcessEffectAssetManager::instance().get(instance.effect);
            std::vector<float> parameters;
            parameters.reserve(asset->parameters.size());
            for (std::size_t index = 0; index < asset->parameters.size(); ++index)
                parameters.push_back(instance.parameters[index]);
            const AssetGuid effectGuid(reference.guid.high, reference.guid.low);
            effects.push_back(CreatePostProcessEffectInstanceData(builder, &effectGuid,
                builder.CreateString(reference.path), instance.enabled, builder.CreateVector(parameters)));
        }
        const auto data = CreatePostProcessProfileData(builder, &guid, builder.CreateString(profile.name),
            settings, builder.CreateVector(effects));
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_POST_PROCESS_PROFILE_VERSION, 0);
        FinishPostProcessProfileFileBuffer(builder, CreatePostProcessProfileFile(builder, header, data));
        return FlatBufferWriter{}.saveAtomic(path, { builder.GetBufferPointer(), builder.GetSize() });
    }

    bool PostProcessProfileSerializer::load(const std::filesystem::path& path,
        Engine::PostProcessProfile& profile) const
    {
        FlatBufferReader reader;
        if (!reader.open(path))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!reader.hasIdentifier("PPPF") || !VerifyPostProcessProfileFileBuffer(verifier))
        {
            LOG_ERROR("[PostProcess] Invalid Profile identifier or FlatBuffers data: {}.", String::fromWide(path.c_str()).view());
            return false;
        }
        const auto* file = GetPostProcessProfileFile(reader.data());
        const auto* source = file->profile();
        if (file->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || file->header()->asset_version() < MINIMUM_SUPPORTED_POST_PROCESS_PROFILE_VERSION
            || file->header()->asset_version() > CURRENT_POST_PROCESS_PROFILE_VERSION
            || source->guid() == nullptr || source->name()->size() == 0)
        {
            LOG_ERROR("[PostProcess] Unsupported Profile version or missing identity: {}.", String::fromWide(path.c_str()).view());
            return false;
        }
        Engine::PostProcessProfile loaded;
        loaded.guid = { source->guid()->high(), source->guid()->low() };
        loaded.name = source->name()->str();
        if (!loaded.guid.isValid() || loaded.name.find('\0') != std::string::npos)
        {
            LOG_ERROR("[PostProcess] Invalid Profile identity: {}.", String::fromWide(path.c_str()).view());
            return false;
        }
        if (!readPostProcessSettings(source->settings(), loaded.settings))
            return false;
        if (file->header()->asset_version() >= 2 && source->custom_effects() != nullptr)
        {
            loaded.customEffects.reserve(source->custom_effects()->size());
            for (const auto* serialized : *source->custom_effects())
            {
                if (serialized == nullptr || serialized->guid() == nullptr || serialized->path() == nullptr
                    || serialized->parameters() == nullptr || serialized->path()->str().find('\0') != std::string::npos)
                {
                    LOG_ERROR("[PostProcess] Profile contains an invalid custom Effect reference.");
                    return false;
                }
                const PostProcessEffectReference reference{
                    { serialized->guid()->high(), serialized->guid()->low() }, serialized->path()->str()
                };
                PostProcessEffectHandle handle;
                if (!readPostProcessEffectReference(reference, handle))
                    return false;
                const auto effect = PostProcessEffectAssetManager::instance().get(handle);
                if (!effect || serialized->parameters()->size() != effect->parameters.size())
                {
                    LOG_ERROR("[PostProcess] Custom Effect parameter layout does not match the referenced asset.");
                    return false;
                }
                PostProcessEffectInstance instance;
                instance.effect = handle;
                instance.enabled = serialized->enabled();
                for (std::size_t index = 0; index < effect->parameters.size(); ++index)
                    instance.parameters[index] = serialized->parameters()->Get(static_cast<flatbuffers::uoffset_t>(index));
                loaded.customEffects.push_back(std::move(instance));
            }
        }
        if (!validateEffectInstances(loaded.customEffects))
            return false;
        profile = std::move(loaded);
        return true;
    }
}
