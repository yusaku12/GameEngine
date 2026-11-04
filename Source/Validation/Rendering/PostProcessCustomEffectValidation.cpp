#include "Pch.h"
#include "Validation\Rendering\PostProcessCustomEffectValidation.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Assets\PostProcess\PostProcessProfileManager.h"
#include "Assets\PostProcess\Serialization\PostProcessEffectSerializer.h"
#include "Assets\PostProcess\Serialization\PostProcessProfileSerializer.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\PostProcessProfile_generated.h"
#include "Graphics\DirectX12\Diagnostics\FrameCapture.h"
#include "Graphics\DirectX12\Rendering\Renderer.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <cstring>

namespace Engine
{
    namespace
    {
        struct Checks
        {
            std::size_t count = 0;
            bool expect(const bool condition, const char* name)
            {
                if (!condition)
                {
                    LOG_ERROR("[CustomEffectValidation] FAILED: {}.", name);
                    return false;
                }
                ++count;
                return true;
            }
        };

        struct Fixtures
        {
            std::vector<PostProcessProfileHandle> profiles;
            PostProcessEffectHandle effect;
            ~Fixtures()
            {
                auto& profilesManager = PostProcessProfileManager::instance();
                for (const auto handle : profiles)
                    if (profilesManager.get(handle))
                        profilesManager.unload(handle);
                if (effect.isValid() && PostProcessEffectAssetManager::instance().get(effect))
                    PostProcessEffectAssetManager::instance().unload(effect);
            }
        };

        float half(const float value)
        {
            return DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(value));
        }

        float toneMap(const float value)
        {
            const double bounded = std::clamp(static_cast<double>(value), 0.0, 65504.0);
            return static_cast<float>(std::clamp(bounded * (2.51 * bounded + 0.03)
                / (bounded * (2.43 * bounded + 0.59) + 0.14), 0.0, 1.0));
        }

        unsigned int srgbByte(const float linear)
        {
            const double value = std::clamp(static_cast<double>(linear), 0.0, 1.0);
            const double encoded = value <= 0.0031308 ? value * 12.92
                : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
            return static_cast<unsigned int>(std::lround(encoded * 255));
        }

        bool matchesReference(const DX12FrameCapture& capture, const PostProcessStage stage,
            unsigned int& maximumError)
        {
            const auto* const hdr = capture.hdr.GetImage(0, 0, 0);
            const auto* const display = capture.display.GetImage(0, 0, 0);
            if (!hdr || !display || hdr->format != DXGI_FORMAT_R16G16B16A16_FLOAT
                || display->format != DXGI_FORMAT_R8G8B8A8_UNORM
                || hdr->width != display->width || hdr->height != display->height)
                return false;
            maximumError = 0;
            for (std::size_t y = 0; y < hdr->height; ++y)
                for (std::size_t x = 0; x < hdr->width; ++x)
                {
                    std::array<DirectX::PackedVector::HALF, 4> source{};
                    std::memcpy(source.data(), hdr->pixels + y * hdr->rowPitch + x * 8, 8);
                    const auto* const actual = display->pixels + y * display->rowPitch + x * 4;
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const float original = DirectX::PackedVector::XMConvertHalfToFloat(source[channel]);
                        const float linearBase = stage == PostProcessStage::BeforeToneMapping
                            ? original : half(toneMap(original));
                        const float effected = half(linearBase * 2.0f);
                        const float blended = half(std::lerp(linearBase, effected, 0.25f));
                        const float finalLinear = stage == PostProcessStage::BeforeToneMapping
                            ? toneMap(blended) : blended;
                        const auto expected = srgbByte(finalLinear);
                        const unsigned int error = expected > actual[channel]
                            ? expected - actual[channel] : actual[channel] - expected;
                        maximumError = std::max(maximumError, error);
                    }
                    maximumError = std::max(maximumError,
                        static_cast<unsigned int>(actual[3] > 255 ? actual[3] - 255 : 255 - actual[3]));
                }
            return maximumError <= 2;
        }

        bool saveLegacyProfile(const std::filesystem::path& path, const PostProcessProfile& profile)
        {
            flatbuffers::FlatBufferBuilder builder;
            const Serialization::AssetGuid guid(profile.guid.high, profile.guid.low);
            const auto settings = Serialization::writePostProcessSettings(builder, profile.settings);
            const auto data = Serialization::CreatePostProcessProfileData(
                builder, &guid, builder.CreateString(profile.name), settings);
            const auto header = Serialization::CreateFileHeader(builder, Serialization::CURRENT_SCHEMA_VERSION, 1, 0);
            Serialization::FinishPostProcessProfileFileBuffer(builder,
                Serialization::CreatePostProcessProfileFile(builder, header, data));
            return Serialization::FlatBufferWriter{}.save(path, { builder.GetBufferPointer(), builder.GetSize() });
        }
    }

    bool runPostProcessCustomEffectValidation(DX12Renderer& renderer, const std::filesystem::path& directory,
        const PostProcessValidationRender& render, const PostProcessValidationVerifySaved& verifySaved,
        PostProcessEffectHandle& retainedEffect)
    {
        Checks checks;
        Fixtures fixtures;
        auto& effectManager = PostProcessEffectAssetManager::instance();
        auto& profileManager = PostProcessProfileManager::instance();
        const auto effectPath = directory / L"custom-gain.posteffect";
        const auto profilePath = directory / L"custom-gain.postprofile";
        PostProcessEffectAsset definition;
        definition.name = "Validation Gain";
        definition.sourcePath = "Assets\\Shaders\\PostProcessValidation.hlsl";
        definition.entryPoint = "psCustomGain";
        definition.stage = PostProcessStage::BeforeToneMapping;
        definition.resolutionDivisor = 1;
        definition.parameters.push_back({ "Gain", 1.0f, 0.0f, 4.0f });
        fixtures.effect = effectManager.create(definition);
        if (!checks.expect(fixtures.effect.isValid(), "compile new HLSL Effect asset")
            || !checks.expect(effectManager.save(fixtures.effect, effectPath), "save Effect descriptor"))
            return false;
        const auto initialEffect = effectManager.get(fixtures.effect);
        if (!checks.expect(initialEffect && initialEffect->pixelShader && initialEffect->pixelShader->isCompiled(),
            "publish immutable compiled shader"))
            return false;

        PostProcessProfile authored;
        authored.name = "Custom Effect Profile";
        PostProcessEffectInstance instance;
        instance.effect = fixtures.effect;
        instance.parameters[0] = 2.0f;
        authored.customEffects.push_back(instance);
        auto profileHandle = profileManager.create(std::move(authored));
        fixtures.profiles.push_back(profileHandle);
        if (!checks.expect(profileHandle.isValid() && profileManager.save(profileHandle, profilePath),
            "save Profile effect reference and parameters"))
            return false;
        auto loadedSnapshot = profileManager.get(profileHandle);
        if (!checks.expect(loadedSnapshot && loadedSnapshot->customEffects.size() == 1
            && loadedSnapshot->customEffects.front().parameters[0] == 2.0f, "Profile stores ordered effect values"))
            return false;
        loadedSnapshot.reset();
        if (!checks.expect(profileManager.unload(profileHandle) && effectManager.unload(fixtures.effect),
            "release assets before cold reload"))
            return false;
        fixtures.profiles.clear();
        fixtures.effect = {};

        fixtures.effect = effectManager.load(effectPath);
        profileHandle = profileManager.load(profilePath);
        fixtures.profiles.push_back(profileHandle);
        if (!checks.expect(fixtures.effect.isValid() && profileHandle.isValid(), "cold-load effect and Profile assets"))
            return false;
        loadedSnapshot = profileManager.get(profileHandle);
        const auto coldAsset = effectManager.get(fixtures.effect);
        if (!checks.expect(loadedSnapshot && loadedSnapshot->customEffects.size() == 1
            && loadedSnapshot->customEffects.front().effect == fixtures.effect
            && loadedSnapshot->customEffects.front().parameters[0] == 2.0f,
            "resolve saved Profile reference after cold load")
            || !checks.expect(coldAsset && coldAsset->pixelShader && coldAsset->pixelShader->isCompiled(),
                "load the precompiled runtime bytecode without DXC"))
            return false;

        const auto originalShader = coldAsset->pixelShader;
        auto invalidDefinition = *coldAsset;
        invalidDefinition.entryPoint = "missingPostProcessEntry";
        if (!checks.expect(!effectManager.update(fixtures.effect, std::move(invalidDefinition)),
            "reject invalid HLSL entry point")
            || !checks.expect(effectManager.get(fixtures.effect)->pixelShader == originalShader,
                "failed compile preserves previous shader snapshot"))
            return false;

        PostProcessProfile legacy;
        legacy.guid = ObjectGUID::generate();
        legacy.name = "Legacy Profile";
        const auto legacyPath = directory / L"custom-effect-legacy.postprofile";
        if (!checks.expect(saveLegacyProfile(legacyPath, legacy), "write legacy Profile fixture"))
            return false;
        PostProcessProfile legacyLoaded;
        if (!checks.expect(Serialization::PostProcessProfileSerializer{}.load(legacyPath, legacyLoaded)
            && legacyLoaded.customEffects.empty(), "load version 1 Profile without custom effects"))
            return false;

        const auto profile = profileManager.get(profileHandle);
        if (!profile || profile->customEffects.size() != 1
            || !renderer.setPostProcessPasses({}) || !renderer.resize(320, 180))
        {
            LOG_ERROR("[CustomEffectValidation] Could not prepare the custom Effect render fixture.");
            return false;
        }
        PostProcessCameraSnapshot baselineCamera;
        baselineCamera.enabled = true;
        if (!render(320, 180, {}, baselineCamera, PostProcessValidationFixture::Panels))
            return false;
        DX12FrameCapture baseline;
        if (!renderer.captureFrame(baseline))
            return false;

        struct CaptureResult
        {
            PostProcessStage stage;
            unsigned int maximumError = 0;
        };
        std::array<CaptureResult, 2> results{};
        const std::array stages{ PostProcessStage::BeforeToneMapping, PostProcessStage::AfterToneMapping };
        for (std::size_t index = 0; index < stages.size(); ++index)
        {
            auto candidate = *effectManager.get(fixtures.effect);
            candidate.stage = stages[index];
            if (!checks.expect(effectManager.update(fixtures.effect, std::move(candidate))
                && effectManager.save(fixtures.effect), "recompile and persist HDR/LDR stage"))
                return false;
            PostProcessCameraSnapshot camera;
            camera.enabled = true;
            camera.weight = 0.5f;
            for (const auto& effect : profile->customEffects)
                if (effect.enabled && !appendPostProcessEffectSnapshot(camera,
                    effect.effect, effect.parameters, 0.5f))
                    return false;
            if (!checks.expect(camera.customEffects.size() == 1
                && camera.customEffects.front().stage == stages[index]
                && camera.customEffects.front().contribution == 0.5f,
                "snapshot contains immutable stage and Volume contribution"))
                return false;
            if (!render(320, 180, {}, camera, PostProcessValidationFixture::Panels))
                return false;
            DX12FrameCapture captured;
            if (!renderer.captureFrame(captured) || !renderer.validateDebugMessages()
                || renderer.getCameraPostProcessSnapshot().customEffects != camera.customEffects
                || renderer.getPostProcessStatistics().effectPasses != 1
                || !matchesReference(captured, stages[index], results[index].maximumError))
            {
                LOG_ERROR("[CustomEffectValidation] GPU output did not match the independent HDR/LDR reference.");
                return false;
            }
            results[index].stage = stages[index];
            const std::wstring name = index == 0 ? L"custom-effect-hdr" : L"custom-effect-ldr";
            if (!captured.save(directory, name) || !verifySaved(captured, directory, name))
                return false;
        }
        if (!renderer.setPostProcessPasses({}))
            return false;
        std::ofstream report(directory / L"custom-effects-results.json", std::ios::binary);
        report << "{\n  \"phase\": 5,\n  \"checks\": " << checks.count
            << ",\n  \"cases\": 2,\n  \"volumeContribution\": 0.5,\n  \"cameraWeight\": 0.5,\n"
            << "  \"expectedEffectiveContribution\": 0.25,\n  \"maxDisplayErrorBytes\": ["
            << results[0].maximumError << ", " << results[1].maximumError
            << "],\n  \"savedImagesMatchReadback\": true,\n  \"passed\": true\n}\n";
        report.close();
        if (!report)
        {
            LOG_ERROR("[CustomEffectValidation] Cannot save Phase 5 report.");
            return false;
        }
        retainedEffect = fixtures.effect;
        fixtures.effect = {};
        LOG_INFO("[CustomEffectValidation] {} checks and 2 Camera GPU cases passed.", checks.count);
        return true;
    }
}
