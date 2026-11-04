#include "Pch.h"
#include "Validation\Rendering\PostProcessAuthoringValidation.h"
#include "Assets\PostProcess\PostProcessProfileManager.h"
#include "Assets\PostProcess\Serialization\PostProcessProfileSerializer.h"
#include "Components\Rendering\CameraComponent.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Containers\String.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Generated\FlatBuffers\CameraComponent_generated.h"
#include "Generated\FlatBuffers\PostProcessProfile_generated.h"
#include <imgui.h>
#include <cmath>
#include <numeric>

namespace Engine
{
    namespace
    {
        struct FixtureProfiles
        {
            std::vector<PostProcessProfileHandle> handles;
            ~FixtureProfiles()
            {
                auto& manager = PostProcessProfileManager::instance();
                for (const auto handle : handles)
                    if (manager.get(handle))
                        manager.unload(handle);
            }
        };

        struct Checks
        {
            std::size_t passed = 0;
            bool expect(const bool condition, const char* name)
            {
                if (!condition)
                {
                    LOG_ERROR("[PostProcessValidation] FAILED: {}.", name);
                    return false;
                }
                ++passed;
                return true;
            }
        };

        bool hasNeutralExtendedGrading(const PostProcessSettings& settings)
        {
            const auto neutral = []<class T>(const PostProcessParameter<T>& parameter, const T& value)
            {
                return parameter.value == value && !parameter.overrideState;
            };
            return neutral(settings.colorTemperature, 6500.0f)
                && neutral(settings.colorTint, 0.0f)
                && neutral(settings.colorGamma, 1.0f)
                && neutral(settings.shadowSaturation, 1.0f)
                && neutral(settings.shadowContrast, 1.0f)
                && neutral(settings.shadowGamma, 1.0f)
                && neutral(settings.shadowGain, 1.0f)
                && neutral(settings.midtoneSaturation, 1.0f)
                && neutral(settings.midtoneContrast, 1.0f)
                && neutral(settings.midtoneGamma, 1.0f)
                && neutral(settings.midtoneGain, 1.0f)
                && neutral(settings.highlightSaturation, 1.0f)
                && neutral(settings.highlightContrast, 1.0f)
                && neutral(settings.highlightGamma, 1.0f)
                && neutral(settings.highlightGain, 1.0f)
                && neutral(settings.colorOffset, Vector3::Zero)
                && neutral(settings.shadowColor, Vector3::Zero)
                && neutral(settings.midtoneColor, Vector3::Zero)
                && neutral(settings.highlightColor, Vector3::Zero)
                && neutral(settings.chromaticAberrationEnabled, false)
                && neutral(settings.chromaticAberrationIntensity, 0.0f)
                && neutral(settings.chromaticAberrationStartOffset, 0.5f)
                && neutral(settings.filmGrainEnabled, false)
                && neutral(settings.filmGrainIntensity, 0.0f)
                && neutral(settings.filmGrainResponse, 0.8f);
        }

        std::vector<std::uint8_t> cameraPayload(const float weight, const PostProcessSettings* settings,
            const ObjectGUID guid = {}, const std::string& path = {})
        {
            flatbuffers::FlatBufferBuilder builder;
            const Serialization::Vec4 viewport(0, 0, 1, 1);
            const Serialization::Vec4 background(0.08f, 0.16f, 0.24f, 1);
            const Serialization::AssetGuid profileGuid(guid.high, guid.low);
            const auto overrides = settings ? Serialization::writePostProcessSettings(builder, *settings)
                : flatbuffers::Offset<Serialization::PostProcessSettingsData>{};
            const auto pathOffset = builder.CreateString(path);
            const auto root = Serialization::CreateCameraComponentPayload(builder,
                0, 60, 0.1f, 1000, 5, &viewport, 1, &background, UINT32_MAX, false,
                true, weight, &profileGuid, pathOffset, overrides);
            Serialization::FinishCameraComponentPayloadBuffer(builder, root);
            return { builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize() };
        }

        bool saveRawProfile(const std::filesystem::path& path, const PostProcessProfile& profile,
            const std::uint32_t version, const bool omitSettings = false)
        {
            flatbuffers::FlatBufferBuilder builder;
            const Serialization::AssetGuid guid(profile.guid.high, profile.guid.low);
            const auto settings = omitSettings ? Serialization::CreatePostProcessSettingsData(builder)
                : Serialization::writePostProcessSettings(builder, profile.settings);
            const auto data = Serialization::CreatePostProcessProfileData(builder,
                &guid, builder.CreateString(profile.name), settings);
            const auto header = Serialization::CreateFileHeader(builder, Serialization::CURRENT_SCHEMA_VERSION, version, 0);
            Serialization::FinishPostProcessProfileFileBuffer(builder,
                Serialization::CreatePostProcessProfileFile(builder, header, data));
            return Serialization::FlatBufferWriter{}.save(path, { builder.GetBufferPointer(), builder.GetSize() });
        }

        flatbuffers::Offset<Serialization::PostProcessSettingsData> createPhase6SettingsWithoutTemporalSettings(
            flatbuffers::FlatBufferBuilder& builder, const PostProcessSettings& settings)
        {
            const auto toFloat = [](const auto& value)
            {
                return Serialization::PostProcessFloat(value.value, value.overrideState);
            };
            const auto toBool = [](const auto& value)
            {
                return Serialization::PostProcessBool(value.value, value.overrideState);
            };
            const auto toColor = [](const auto& value)
            {
                return Serialization::PostProcessColor(
                    Serialization::Vec3(value.value.x, value.value.y, value.value.z), value.overrideState);
            };
            const auto bloomEnabled = toBool(settings.bloomEnabled);
            const auto bloomIntensity = toFloat(settings.bloomIntensity);
            const auto bloomThreshold = toFloat(settings.bloomThreshold);
            const auto bloomScatter = toFloat(settings.bloomScatter);
            const auto exposureEnabled = toBool(settings.exposureEnabled);
            const auto exposureEV = toFloat(settings.exposureEV);
            const auto colorAdjustmentsEnabled = toBool(settings.colorAdjustmentsEnabled);
            const auto contrast = toFloat(settings.contrast);
            const auto saturation = toFloat(settings.saturation);
            const auto colorFilter = toColor(settings.colorFilter);
            const auto vignetteEnabled = toBool(settings.vignetteEnabled);
            const auto vignetteIntensity = toFloat(settings.vignetteIntensity);
            const auto vignetteSmoothness = toFloat(settings.vignetteSmoothness);
            const auto vignetteRoundness = toFloat(settings.vignetteRoundness);
            const auto vignetteColor = toColor(settings.vignetteColor);
            const auto fxaaEnabled = toBool(settings.fxaaEnabled);
            const auto fxaaSubpixel = toFloat(settings.fxaaSubpixel);
            const auto fxaaEdgeThreshold = toFloat(settings.fxaaEdgeThreshold);
            const auto fxaaEdgeThresholdMin = toFloat(settings.fxaaEdgeThresholdMin);

            return Serialization::CreatePostProcessSettingsData(builder,
                &bloomEnabled, &bloomIntensity, &bloomThreshold, &bloomScatter,
                &exposureEnabled, &exposureEV, &colorAdjustmentsEnabled, &contrast, &saturation,
                &colorFilter, &vignetteEnabled, &vignetteIntensity, &vignetteSmoothness,
                &vignetteRoundness, &vignetteColor, &fxaaEnabled, &fxaaSubpixel,
                &fxaaEdgeThreshold, &fxaaEdgeThresholdMin);
        }

        bool savePhase6ProfileWithoutTemporalSettings(const std::filesystem::path& path,
            const PostProcessProfile& profile)
        {
            flatbuffers::FlatBufferBuilder builder;
            const auto serializedSettings = createPhase6SettingsWithoutTemporalSettings(builder, profile.settings);
            const Serialization::AssetGuid guid(profile.guid.high, profile.guid.low);
            const auto data = Serialization::CreatePostProcessProfileData(
                builder, &guid, builder.CreateString(profile.name), serializedSettings);
            const auto header = Serialization::CreateFileHeader(builder, Serialization::CURRENT_SCHEMA_VERSION, 3, 0);
            Serialization::FinishPostProcessProfileFileBuffer(builder,
                Serialization::CreatePostProcessProfileFile(builder, header, data));
            return Serialization::FlatBufferWriter{}.save(path, { builder.GetBufferPointer(), builder.GetSize() });
        }

        std::vector<std::uint8_t> phase6CameraPayloadWithoutTemporalSettings(const PostProcessSettings& settings)
        {
            flatbuffers::FlatBufferBuilder builder;
            const Serialization::Vec4 viewport(0, 0, 1, 1);
            const Serialization::Vec4 background(0.08f, 0.16f, 0.24f, 1);
            const Serialization::AssetGuid profileGuid{};
            const auto overrides = createPhase6SettingsWithoutTemporalSettings(builder, settings);
            const auto root = Serialization::CreateCameraComponentPayload(builder,
                0, 60, 0.1f, 1000, 5, &viewport, 1, &background, UINT32_MAX, false,
                true, 1, &profileGuid, builder.CreateString(""), overrides);
            Serialization::FinishCameraComponentPayloadBuffer(builder, root);
            return { builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize() };
        }

        std::vector<std::uint8_t> legacyCameraPayload()
        {
            flatbuffers::FlatBufferBuilder builder;
            const Serialization::Vec4 viewport(0, 0, 1, 1);
            const Serialization::Vec4 background(0.08f, 0.16f, 0.24f, 1);
            // Only the original v1 fields are written; the new schema must not require v2 fields.
            Serialization::FinishCameraComponentPayloadBuffer(builder,
                Serialization::CreateCameraComponentPayload(builder, 0, 60, 0.1f, 1000, 5,
                    &viewport, 1, &background, UINT32_MAX, false));
            return { builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize() };
        }

        bool validateInspector(Checks& checks, CameraComponent& camera, FixtureProfiles& fixtures)
        {
            struct Context
            {
                ImGuiContext* previous = ImGui::GetCurrentContext();
                std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> owned{
                    ImGui::CreateContext(), &ImGui::DestroyContext };
                ~Context()
                {
                    owned.reset();
                    ImGui::SetCurrentContext(previous);
                }
            } context;
            if (!checks.expect(context.owned != nullptr, "headless ImGui context"))
                return false;
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.DisplaySize = { 2600, 5000 };
            io.DeltaTime = 1.0f / 60;
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            if (!checks.expect(pixels && width > 0 && height > 0, "ImGui font atlas"))
                return false;
            io.Fonts->SetTexID(static_cast<ImTextureID>(1));
            ImVec2 origin;
            const auto frame = [&camera, &origin]
            {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({ 10, 10 }, ImGuiCond_Always);
                ImGui::SetNextWindowSize({ 2500, 4800 }, ImGuiCond_Always);
                ImGui::Begin("PostEffect Inspector Validation", nullptr,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
                origin = ImGui::GetCursorScreenPos();
                ImGui::PushID("PostEffects");
                for (const char* layer : { "Edit Shared Profile", "Camera Overrides" })
                {
                    ImGui::GetStateStorage()->SetInt(ImGui::GetID(layer), 1);
                    ImGui::PushID(layer);
                    for (const char* effect : { "Bloom", "Exposure", "Color Grading", "Vignette",
                        "Chromatic Aberration", "Film Grain", "FXAA", "Temporal Anti-Aliasing",
                        "Motion Blur", "Depth of Field",
                        "Color Grading LUT", "Automatic Exposure", "Shadows", "Midtones", "Highlights" })
                        ImGui::GetStateStorage()->SetInt(ImGui::GetID(effect), 1);
                    ImGui::PopID();
                }
                ImGui::PopID();
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                camera.drawImGui();
                ImGui::End();
                ImGui::Render();
                return ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0;
            };
            if (!checks.expect(frame(), "all Inspector authoring sections render"))
                return false;
            const auto originalProfile = camera.getPostProcessProfile();
            const auto originalSettings = camera.getPostProcessOverrides();
            const auto asset = PostProcessProfileManager::instance().get(originalProfile);
            const float row = ImGui::GetFrameHeightWithSpacing();
            const float toggleY = origin.y + row + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeight() * 0.5f;
            const auto click = [&io, &frame, &origin](const float y)
            {
                io.AddMousePosEvent(origin.x + 8, y);
                if (!frame()) return false;
                io.AddMouseButtonEvent(0, true);
                if (!frame()) return false;
                io.AddMouseButtonEvent(0, false);
                return frame();
            };
            if (!checks.expect(click(toggleY) && !camera.isPostProcessEnabled()
                && camera.getPostProcessOverrides() == originalSettings
                && PostProcessProfileManager::instance().get(originalProfile) == asset,
                "Inspector enable toggle preserves local/shared values"))
                return false;
            if (!click(toggleY + row * 3))
                return false;
            const auto created = camera.getPostProcessProfile();
            if (created != originalProfile && created.isValid())
                fixtures.handles.push_back(created);
            if (!checks.expect(created != originalProfile && created.isValid()
                && PostProcessProfileManager::instance().isDirty(created),
                "Inspector New Profile creates and assigns an independent unsaved asset"))
                return false;
            camera.setPostProcessEnabled(true);
            return camera.setPostProcessProfile(originalProfile);
        }
    }

    bool runPostProcessAuthoringValidation(const std::filesystem::path& directory,
        PostProcessCameraSnapshot& snapshot)
    {
        Checks checks;
        FixtureProfiles fixtures;
        auto& manager = PostProcessProfileManager::instance();
        const auto initialHandles = manager.getAllHandles().size();
        const auto profilePath = directory / L"authoring-fixtures" / L"profile.postprofile";
        const auto otherPath = directory / L"authoring-fixtures" / L"other.postprofile";
        Serialization::PostProcessProfileSerializer serializer;
        PostProcessProfile profile;
        profile.name = "Phase2Validation";
        for (std::size_t index = 0; index < POST_PROCESS_FLOATS.size(); ++index)
        {
            const auto& descriptor = POST_PROCESS_FLOATS[index];
            profile.settings.*descriptor.member = {
                std::midpoint(descriptor.minimum, descriptor.maximum), index % 2 == 0 };
        }
        for (const auto& descriptor : POST_PROCESS_SWITCHES)
            profile.settings.*descriptor.member = { true, true };
        profile.settings.colorGradingLutEnabled = { false, true };
        for (const auto& descriptor : POST_PROCESS_COLORS)
            profile.settings.*descriptor.member = { Vector3(0.1f, 0.3f, 0.7f), true };
        profile.settings.exposureEV = { 2.0f, true };
        profile.settings.fxaaEnabled = { true, false };
        const auto handle = manager.create(profile);
        fixtures.handles.push_back(handle);
        if (!checks.expect(handle.isValid() && manager.isDirty(handle), "create dirty Profile")
            || !checks.expect(manager.save(handle, profilePath) && !manager.isDirty(handle), "atomic Profile Save As clears dirty state"))
            return false;
        profile = *manager.get(handle);
        PostProcessProfile decoded;
        if (!checks.expect(serializer.load(profilePath, decoded) && decoded == profile, "all values/override flags round trip")
            || !checks.expect(manager.load(profilePath) == handle, "path cache")
            || !checks.expect(manager.load(profilePath.parent_path() / L"PROFILE.POSTPROFILE") == handle, "Windows path case aliases share one asset")
            || !checks.expect(manager.findByGuid(profile.guid) == handle, "GUID lookup"))
            return false;
        auto phase6Profile = profile;
        phase6Profile.guid = ObjectGUID::generate();
        phase6Profile.name = "Phase6LegacySettings";
        const auto phase6Path = directory / L"authoring-fixtures" / L"phase6-legacy.postprofile";
        PostProcessProfile phase6Decoded;
        if (!checks.expect(savePhase6ProfileWithoutTemporalSettings(phase6Path, phase6Profile)
            && serializer.load(phase6Path, phase6Decoded)
            && phase6Decoded.settings.autoExposureSpeedUp.value == 3.0f
            && phase6Decoded.settings.autoExposureSpeedDown.value == 1.0f
            && phase6Decoded.settings.bloomSizeScale.value == 1.0f,
            "Phase 6 Profile defaults missing temporal adaptation settings"))
            return false;
        if (!checks.expect(hasNeutralExtendedGrading(phase6Decoded.settings),
            "legacy Profile defaults missing White Balance, tonal grading, chromatic aberration and film grain"))
            return false;
        CameraComponent phase6Camera;
        const auto phase6CameraPayload = phase6CameraPayloadWithoutTemporalSettings(phase6Profile.settings);
        if (!checks.expect(phase6Camera.deserializePayload(4, phase6CameraPayload)
            && phase6Camera.getPostProcessOverrides().autoExposureSpeedUp.value == 3.0f
            && phase6Camera.getPostProcessOverrides().autoExposureSpeedDown.value == 1.0f
            && phase6Camera.getPostProcessOverrides().bloomSizeScale.value == 1.0f,
            "Camera v4 defaults missing temporal adaptation settings"))
            return false;
        if (!checks.expect(hasNeutralExtendedGrading(phase6Camera.getPostProcessOverrides()),
            "legacy Camera defaults missing White Balance, tonal grading, chromatic aberration and film grain"))
            return false;

        CameraComponent camera;
        camera.setPostProcessEnabled(true);
        PostProcessSettings overrides;
        overrides.exposureEV = { -1.0f, true };
        overrides.bloomIntensity = { 3.0f, true };
        overrides.colorFilter = { Vector3(0.9f, 0.8f, 0.7f), true };
        if (!camera.setPostProcessProfile(handle) || !camera.setPostProcessWeight(0.4f)
            || !camera.setPostProcessOverrides(overrides))
            return false;
        const RenderView frozen = camera.buildRenderView();
        CameraRenderSubmissionQueue::instance().submit(frozen);
        PostProcessProfile edited = profile;
        edited.settings.exposureEV = { 3.0f, true };
        edited.settings.bloomScatter = { 0.2f, true };
        const auto oldAsset = manager.get(handle);
        if (!checks.expect(manager.update(handle, edited) && manager.isDirty(handle), "immutable Profile replacement marks dirty")
            || !checks.expect(*oldAsset == profile && oldAsset != manager.get(handle), "old asset snapshot remains unchanged"))
            return false;
        RenderView queued;
        if (!checks.expect(CameraRenderSubmissionQueue::instance().consume(queued) && queued.postProcess == frozen.postProcess,
            "queued RenderView remains immutable after edit"))
            return false;
        const auto resolved = camera.buildRenderView().postProcess;
        const auto matchesResolvedSettings = [&resolved](PostProcessCameraSnapshot candidate)
        {
            auto expected = resolved;
            expected.cameraGuid = candidate.cameraGuid;
            expected.deltaTimeSeconds = candidate.deltaTimeSeconds;
            return candidate == expected;
        };
        if (!checks.expect(resolved.settings.exposureEV.value == -1
            && resolved.settings.bloomIntensity.value == 3 && resolved.settings.bloomScatter.value == 0.2f
            && !resolved.settings.exposureEV.overrideState && resolved.weight == 0.4f,
            "defaults then Profile then local overrides")
            || !checks.expect(manager.save(handle) && !manager.isDirty(handle), "save updated Profile")
            || !checks.expect(manager.reload(handle) && *manager.get(handle) == edited && !manager.isDirty(handle), "explicit reload"))
            return false;
        CameraComponent clone;
        if (!checks.expect(camera.copyConfigurationTo(clone)
            && clone.getPostProcessProfile() == handle && clone.buildRenderView().postProcess == resolved,
            "Play configuration clone shares only immutable asset"))
            return false;
        auto cloneOverrides = clone.getPostProcessOverrides();
        cloneOverrides.exposureEV = { 7.0f, true };
        if (!checks.expect(clone.setPostProcessOverrides(cloneOverrides)
            && camera.buildRenderView().postProcess == resolved && *manager.get(handle) == edited,
            "clone/local editing does not change original camera or Profile"))
            return false;

        std::vector<std::uint8_t> saved;
        CameraComponent restored;
        if (!checks.expect(camera.serializePayload(saved)
            && restored.deserializePayload(camera.getPayloadVersion(), saved)
            && restored.getPostProcessOverrides() == overrides && restored.buildRenderView().postProcess == resolved,
            "Camera v2 settings/GUID/path round trip"))
            return false;
        if (!validateInspector(checks, camera, fixtures))
            return false;
        camera.setPostProcessEnabled(false);
        if (!checks.expect(!camera.buildRenderView().postProcess.enabled && camera.getPostProcessOverrides() == overrides,
            "disable preserves authored values"))
            return false;
        camera.setPostProcessEnabled(true);

        Scene scene(ObjectGUID::generate(), "Phase2Authoring");
        auto* const object = scene.createGameObject("ProfileCamera");
        auto* const sceneCamera = object ? object->addComponent<CameraComponent>() : nullptr;
        if (!checks.expect(sceneCamera && camera.copyConfigurationTo(*sceneCamera), "Scene Camera fixture"))
            return false;
        sceneCamera->setRenderTargetSize(1280, 720);
        sceneCamera->setViewport({ 0.1f, 0.2f, 0.7f, 0.6f });
        Scene playClone(ObjectGUID::generate(), "PlayClone");
        const std::array clonePairs{ SceneClonePair{ scene, playClone } };
        if (!checks.expect(cloneScenes(clonePairs), "real Play scene cloning path"))
            return false;
        auto* const clonedObject = playClone.find(object->getGUID());
        auto* const clonedCamera = clonedObject ? clonedObject->getComponent<CameraComponent>() : nullptr;
        if (!checks.expect(clonedCamera && clonedCamera->getPostProcessProfile() == handle
            && matchesResolvedSettings(clonedCamera->buildRenderView().postProcess),
            "scene clone retains Profile and overrides"))
            return false;
        clonedCamera->setRenderTargetSize(1280, 720);
        if (!checks.expect(clonedCamera->getProjectionMatrix() == sceneCamera->getProjectionMatrix(),
            "clone recalculates camera aspect for its own viewport"))
            return false;
        playClone.clear();
        const auto scenePath = directory / L"authoring-fixtures" / L"camera.scene";
        Scene loadedScene(ObjectGUID::generate(), "Destination");
        if (!checks.expect(Serialization::SceneSerializer{}.save(scenePath, scene)
            && Serialization::SceneSerializer{}.load(scenePath, loadedScene), "Scene save/load"))
            return false;
        const auto* const loadedObject = loadedScene.find(object->getGUID());
        const auto* const loadedCamera = loadedObject ? loadedObject->getComponent<CameraComponent>() : nullptr;
        if (!checks.expect(loadedCamera && matchesResolvedSettings(loadedCamera->buildRenderView().postProcess),
            "Scene Camera configuration"))
            return false;
        Prefab prefab;
        Prefab loadedPrefab;
        const auto prefabPath = directory / L"authoring-fixtures" / L"camera.prefab";
        if (!checks.expect(prefab.capture(*object) && Serialization::PrefabSerializer{}.save(prefabPath, prefab)
            && Serialization::PrefabSerializer{}.load(prefabPath, loadedPrefab), "Prefab capture/save/load"))
            return false;
        auto* const instance = loadedPrefab.instantiate(loadedScene);
        const auto* const prefabCamera = instance ? instance->getComponent<CameraComponent>() : nullptr;
        if (!checks.expect(prefabCamera && matchesResolvedSettings(prefabCamera->buildRenderView().postProcess),
            "Prefab Camera restoration"))
            return false;

        LOG_INFO("[PostProcessValidation] Begin expected rejection tests (application errors below are intentional).");
        const auto expectCameraFailure = [&checks, &restored, &saved](const std::uint32_t version,
            const std::span<const std::uint8_t> payload, const char* name)
        {
            std::vector<std::uint8_t> after;
            return checks.expect(!restored.deserializePayload(version, payload)
                && restored.serializePayload(after) && after == saved, name);
        };
        if (!expectCameraFailure(99, saved, "unsupported Camera version preserves state")
            || !expectCameraFailure(2, std::span(saved).first(7), "truncated Camera preserves state")
            || !expectCameraFailure(2, cameraPayload(-1, &overrides), "invalid Camera weight preserves state")
            || !expectCameraFailure(2, cameraPayload(std::numeric_limits<float>::quiet_NaN(), &overrides),
                "nonfinite Camera weight preserves state")
            || !expectCameraFailure(2, cameraPayload(1, nullptr), "missing Camera overrides rejected")
            || !expectCameraFailure(2, cameraPayload(1, &overrides, ObjectGUID::generate(), profilePath.generic_string()),
                "mismatched Profile identity preserves camera"))
            return false;
        if (!checks.expect(manager.getAllHandles().size() == initialHandles + fixtures.handles.size(), "GUID rejection did not register a Profile"))
            return false;
        if (!expectCameraFailure(2, cameraPayload(1, &overrides, profile.guid), "GUID without path rejected")
            || !expectCameraFailure(2, cameraPayload(1, &overrides, {}, profilePath.generic_string()), "path without GUID rejected")
            || !expectCameraFailure(2, cameraPayload(1, &overrides, ObjectGUID::generate(),
                (directory / L"authoring-fixtures" / L"missing.postprofile").generic_string()),
                "missing Profile fails explicitly and preserves camera"))
            return false;
        auto invalidSettings = overrides;
        invalidSettings.colorFilter.value.x = std::numeric_limits<float>::infinity();
        if (!expectCameraFailure(2, cameraPayload(1, &invalidSettings), "nonfinite serialized Camera color rejected")
            || !checks.expect(!camera.setPostProcessWeight(2) && camera.getPostProcessWeight() == 0.4f, "setter rejects invalid weight")
            || !checks.expect(!camera.setPostProcessProfile({ 999999, 1 }) && camera.getPostProcessProfile() == handle,
                "setter rejects stale Profile")
            || !checks.expect(!camera.setPostProcessOverrides(invalidSettings) && camera.getPostProcessOverrides() == overrides,
                "setter preserves overrides on invalid color"))
            return false;
        for (const auto& descriptor : POST_PROCESS_FLOATS)
        {
            auto boundary = edited.settings;
            (boundary.*descriptor.member).value = descriptor.minimum;
            if (descriptor.member == &PostProcessSettings::autoExposureMinEV)
                boundary.autoExposureMaxEV.value = std::max(boundary.autoExposureMaxEV.value, descriptor.minimum);
            else if (descriptor.member == &PostProcessSettings::autoExposureMaxEV)
                boundary.autoExposureMinEV.value = std::min(boundary.autoExposureMinEV.value, descriptor.minimum);
            if (!checks.expect(validatePostProcessSettings(boundary), "inclusive scalar minimum accepted"))
                return false;
            boundary = edited.settings;
            (boundary.*descriptor.member).value = descriptor.maximum;
            if (descriptor.member == &PostProcessSettings::autoExposureMinEV)
                boundary.autoExposureMaxEV.value = descriptor.maximum;
            else if (descriptor.member == &PostProcessSettings::autoExposureMaxEV)
                boundary.autoExposureMinEV.value = std::min(boundary.autoExposureMinEV.value, descriptor.maximum);
            if (!checks.expect(validatePostProcessSettings(boundary), "inclusive scalar maximum accepted"))
                return false;
            auto invalid = edited;
            (invalid.settings.*descriptor.member).value = std::nextafter(descriptor.minimum, -INFINITY);
            if (!checks.expect(!manager.update(handle, invalid) && *manager.get(handle) == edited, "scalar lower bound rejected"))
                return false;
            (invalid.settings.*descriptor.member).value = std::nextafter(descriptor.maximum, INFINITY);
            if (!checks.expect(!manager.update(handle, invalid) && *manager.get(handle) == edited, "scalar upper bound rejected"))
                return false;
            (invalid.settings.*descriptor.member).value = std::numeric_limits<float>::quiet_NaN();
            if (!checks.expect(!manager.create(invalid).isValid(), "nonfinite inactive/active parameter rejected"))
                return false;
        }
        const auto other = manager.create({});
        fixtures.handles.push_back(other);
        if (!checks.expect(other.isValid() && manager.save(other, otherPath), "second Profile fixture")
            || !checks.expect(!manager.save(handle, otherPath), "Save As rejects registered path conflict before writing"))
            return false;
        PostProcessProfile otherDecoded;
        if (!checks.expect(serializer.load(otherPath, otherDecoded) && otherDecoded == *manager.get(other),
            "conflicting Save As did not overwrite disk"))
            return false;
        if (!checks.expect(!manager.save(handle, otherPath.parent_path() / L"OTHER.POSTPROFILE"),
            "case-only Save As conflicts cannot overwrite another Profile"))
            return false;
        const auto relocatedPath = directory / L"authoring-fixtures" / L"relocated.postprofile";
        if (!checks.expect(manager.save(other, relocatedPath)
            && manager.getPath(other) == std::filesystem::weakly_canonical(relocatedPath)
            && manager.load(relocatedPath) == other && manager.get(other)->guid == otherDecoded.guid,
            "Save As preserves identity and updates registered path"))
            return false;
        const auto beforeFailure = manager.get(handle);
        if (!saveRawProfile(profilePath, edited, 99)
            || !checks.expect(!manager.reload(handle) && manager.get(handle) == beforeFailure && !manager.isDirty(handle),
                "failed reload preserves Profile snapshot/path/dirty state")
            || !manager.save(handle))
            return false;
        auto invalidIdentity = edited;
        invalidIdentity.guid = {};
        if (!checks.expect(!serializer.save(profilePath, invalidIdentity), "invalid GUID save rejected"))
            return false;
        invalidIdentity = edited;
        invalidIdentity.name.clear();
        if (!checks.expect(!manager.update(handle, invalidIdentity) && manager.get(handle) == beforeFailure,
            "empty Profile name cannot replace snapshot"))
            return false;
        const auto malformedPath = directory / L"authoring-fixtures" / L"malformed.postprofile";
        for (const bool omitSettings : { false, true })
        {
            if (!saveRawProfile(malformedPath, edited, omitSettings ? 1 : 99, omitSettings)
                || !checks.expect(!serializer.load(malformedPath, decoded) && decoded == profile,
                    "unsupported/incomplete Profile preserves load destination"))
                return false;
        }
        auto invalidProfile = edited;
        invalidProfile.settings.bloomIntensity.value = std::numeric_limits<float>::infinity();
        if (!saveRawProfile(malformedPath, invalidProfile, 1)
            || !checks.expect(!serializer.load(malformedPath, decoded) && decoded == profile, "nonfinite disk Profile rejected")
            || !checks.expect(!serializer.save(profilePath, invalidProfile), "invalid save rejected before overwriting")
            || !checks.expect(serializer.load(profilePath, decoded) && decoded == edited, "valid disk Profile preserved"))
            return false;
        const std::array<std::uint8_t, 8> truncated{};
        if (!Serialization::FlatBufferWriter{}.save(malformedPath, truncated)
            || !checks.expect(!serializer.load(malformedPath, decoded) && decoded == edited, "truncated Profile rejected"))
            return false;

        PostProcessProfile unsaved;
        unsaved.settings.exposureEV = { 4, true };
        const auto unsavedHandle = manager.create(unsaved);
        fixtures.handles.push_back(unsavedHandle);
        CameraComponent unsavedCamera;
        CameraComponent unsavedClone;
        std::vector<std::uint8_t> untouched{ 1, 2, 3 };
        if (!checks.expect(unsavedCamera.setPostProcessProfile(unsavedHandle)
            && !unsavedCamera.serializePayload(untouched) && untouched == std::vector<std::uint8_t>{ 1, 2, 3 },
            "unsaved Profile cannot silently lose values in Scene save")
            || !checks.expect(unsavedCamera.copyConfigurationTo(unsavedClone)
                && unsavedClone.getPostProcessProfile() == unsavedHandle, "unsaved Profile supports Play clone"))
            return false;
        CameraComponent legacy;
        const auto legacyBytes = legacyCameraPayload();
        if (!checks.expect(legacy.deserializePayload(1, legacyBytes) && !legacy.isPostProcessEnabled()
            && legacy.getPostProcessOverrides() == PostProcessSettings{} && !legacy.getPostProcessProfile().isValid(),
            "v1 Camera has neutral PostEffect defaults")
            || !checks.expect(legacy.deserializePayload(0, {}) && !legacy.isPostProcessEnabled(), "v0 empty Camera accepted"))
            return false;

        CameraComponent noProfile;
        noProfile.setPostProcessEnabled(true);
        if (!checks.expect(noProfile.setPostProcessOverrides(overrides)
            && noProfile.buildRenderView().postProcess.settings.exposureEV.value == -1,
            "local overrides without a Profile resolve independently"))
            return false;
        const auto unicodePath = directory / L"authoring-fixtures" / L"\u30D7\u30ED\u30D5\u30A1\u30A4\u30EB.postprofile";
        const auto unicodeHandle = manager.create({});
        fixtures.handles.push_back(unicodeHandle);
        CameraComponent unicodeCamera;
        CameraComponent unicodeRestored;
        std::vector<std::uint8_t> unicodePayload;
        if (!checks.expect(unicodeHandle.isValid() && manager.save(unicodeHandle, unicodePath)
            && unicodeCamera.setPostProcessProfile(unicodeHandle) && unicodeCamera.serializePayload(unicodePayload)
            && unicodeRestored.deserializePayload(2, unicodePayload) && unicodeRestored.getPostProcessProfile() == unicodeHandle,
            "Unicode Profile paths round trip as UTF-8"))
            return false;
        const auto unicodeGuid = manager.get(unicodeHandle)->guid;
        if (!checks.expect(manager.unload(unicodeHandle)
            && unicodeRestored.deserializePayload(2, unicodePayload), "Unicode Profile cold load"))
            return false;
        fixtures.handles.push_back(unicodeRestored.getPostProcessProfile());
        if (!checks.expect(manager.get(unicodeRestored.getPostProcessProfile())->guid == unicodeGuid,
            "Unicode cold load retains identity"))
            return false;
        const auto savedHandle = handle;
        snapshot = camera.buildRenderView().postProcess;
        scene.clear();
        loadedScene.clear();
        if (!checks.expect(manager.unload(handle) && !manager.get(savedHandle) && *beforeFailure == edited,
            "unload invalidates handle but retains snapshots"))
            return false;
        const auto beforeMismatchCount = manager.getAllHandles().size();
        if (!checks.expect(!manager.load(profilePath, ObjectGUID::generate()).isValid()
            && manager.getAllHandles().size() == beforeMismatchCount, "cold GUID mismatch has no registry side effects"))
            return false;
        const auto reloaded = manager.load(profilePath, edited.guid);
        fixtures.handles.push_back(reloaded);
        if (!checks.expect(reloaded.isValid() && reloaded != savedHandle && !manager.get(savedHandle),
            "slot reuse cannot resurrect a stale handle"))
            return false;
        if (!checks.expect(!camera.buildRenderView().postProcess.enabled, "stale camera Profile disables effects with diagnostic"))
            return false;
        if (!checks.expect(restored.deserializePayload(2, saved) && restored.getPostProcessProfile() == reloaded,
            "cold Profile load through Camera reference"))
            return false;
        CameraRenderSubmissionQueue::instance().clear();
        std::ofstream report(directory / L"authoring-results.json", std::ios::binary);
        report << "{\n  \"phase\": 2,\n  \"passed\": true,\n  \"checks\": " << checks.passed
            << ",\n  \"profileRoundTrip\": true,\n  \"cameraLegacyCompatibility\": true,\n"
            "  \"sceneAndPrefabRoundTrip\": true,\n  \"snapshotIsolation\": true,\n"
            "  \"failurePreservesState\": true,\n  \"inspectorControls\": true,\n"
            "  \"authoringChecksDoNotExecuteGpuEffects\": true\n}\n";
        report.close();
        if (!report)
        {
            LOG_ERROR("[PostProcessValidation] Cannot save authoring-results.json.");
            return false;
        }
        LOG_INFO("[PostProcessValidation] All {} authoring checks passed.", checks.passed);
        return true;
    }
}
