#include "Pch.h"
#include "Validation\Rendering\PostProcessVolumeValidation.h"
#include "Graphics\DirectX12\Rendering\Renderer.h"
#include "Graphics\DirectX12\Diagnostics\FrameCapture.h"
#include "Components\Rendering\PostProcessVolumeComponent.h"
#include "Components\Rendering\CameraComponent.h"
#include "Core\Scene\SceneManager.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Containers\String.h"
#include "Assets\PostProcess\PostProcessProfileManager.h"
#include "Assets\PostProcess\Serialization\PostProcessProfileSerializer.h"
#include "Generated\FlatBuffers\PostProcessVolumeComponent_generated.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include <imgui.h>
#include <cmath>
#include <cstring>

namespace Engine
{
    namespace
    {
        struct Checks
        {
            std::size_t count = 0;
            bool expect(const bool condition, const char* label)
            {
                if (!condition) { LOG_ERROR("[VolumeValidation] FAILED: {}.", label); return false; }
                ++count;
                return true;
            }
            bool close(const float actual, const float expected, const char* label)
            {
                if (std::abs(actual - expected) > 0.0001f)
                    LOG_ERROR("[VolumeValidation] {}: expected {}, actual {}.", label, expected, actual);
                return expect(std::abs(actual - expected) <= 0.0001f, label);
            }
        };
        struct Fixture
        {
            std::vector<std::unique_ptr<Scene>> previousScenes;
            Scene* previousActive = nullptr;
            Scene previousPersistent{ ObjectGUID::generate(), "Previous persistent" };
            std::vector<PostProcessProfileHandle> profiles;
            bool exchanged = false;
            bool initialize()
            {
                auto& manager = SceneManager::instance();
                if (!manager.exchangeScenes(previousScenes, previousActive))
                    return false;
                if (!manager.getPersistentScene()->exchangeGameObjects(previousPersistent))
                {
                    manager.exchangeScenes(previousScenes, previousActive);
                    return false;
                }
                exchanged = true;
                return true;
            }
            ~Fixture()
            {
                if (exchanged)
                {
                    auto& manager = SceneManager::instance();
                    if (!manager.discardScenes() || !manager.getPersistentScene()->exchangeGameObjects(previousPersistent)
                        || !manager.exchangeScenes(previousScenes, previousActive))
                        LOG_CRITICAL("[VolumeValidation] Cannot restore isolated Scene session.");
                }
                for (const auto profile : profiles)
                    if (PostProcessProfileManager::instance().get(profile))
                        PostProcessProfileManager::instance().unload(profile);
            }
        };
        bool identical(const DirectX::ScratchImage& a, const DirectX::ScratchImage& b)
        {
            const auto* left = a.GetImage(0, 0, 0);
            const auto* right = b.GetImage(0, 0, 0);
            if (!left || !right || left->width != right->width || left->height != right->height || left->format != right->format)
                return false;
            const auto bytes = left->width * DirectX::BitsPerPixel(left->format) / 8;
            for (std::size_t y = 0; y < left->height; ++y)
                if (std::memcmp(left->pixels + y * left->rowPitch, right->pixels + y * right->rowPitch, bytes) != 0)
                    return false;
            return true;
        }
        std::vector<std::uint8_t> payload(const PostProcessVolumeSettings& settings,
            const ObjectGUID guid = {}, const std::string& path = {}, const bool omitSize = false)
        {
            flatbuffers::FlatBufferBuilder builder;
            const Serialization::Vec3 center(settings.center.x, settings.center.y, settings.center.z);
            const Serialization::Vec3 size(settings.boxSize.x, settings.boxSize.y, settings.boxSize.z);
            const Serialization::AssetGuid profileGuid(guid.high, guid.low);
            const auto root = Serialization::CreatePostProcessVolumeComponentPayload(builder,
                static_cast<std::uint8_t>(settings.shape), settings.priority, settings.weight, settings.blendDistance,
                &center, omitSize ? nullptr : &size, settings.sphereRadius, &profileGuid, builder.CreateString(path));
            Serialization::FinishPostProcessVolumeComponentPayloadBuffer(builder, root);
            return { builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize() };
        }
        bool inspector(Checks& checks, PostProcessVolumeComponent& volume, Fixture& fixtures)
        {
            struct Context
            {
                ImGuiContext* previous = ImGui::GetCurrentContext();
                std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> owned{ ImGui::CreateContext(), &ImGui::DestroyContext };
                ~Context() { owned.reset(); ImGui::SetCurrentContext(previous); }
            } context;
            if (!checks.expect(context.owned != nullptr, "Volume ImGui context"))
                return false;
            auto& io = ImGui::GetIO();
            io.IniFilename = io.LogFilename = nullptr;
            io.DisplaySize = { 2400, 4000 }; io.DeltaTime = 1.0f / 60;
            unsigned char* pixels = nullptr; int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            io.Fonts->SetTexID(static_cast<ImTextureID>(1));
            ImVec2 newButton;
            const auto frame = [&]()
            {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({ 10, 10 }, ImGuiCond_Always);
                ImGui::SetNextWindowSize({ 2300, 3900 }, ImGuiCond_Always);
                ImGui::Begin("Volume Inspector Validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
                // Find the button through the same ID used by shared Profile controls.
                ImGui::PushID("VolumeProfile");
                auto* storage = ImGui::GetStateStorage();
                for (const char* section : { "Edit Shared Profile", "Bloom", "Exposure", "Color Adjustments", "Vignette", "FXAA" })
                    storage->SetInt(ImGui::GetID(section), 1);
                ImGui::PopID();
                const auto origin = ImGui::GetCursorScreenPos();
                volume.drawImGui();
                // Global controls: Shape, Priority, Weight, hint, Shared Profile, New Profile.
                newButton = { origin.x + 8, origin.y + 4 * ImGui::GetFrameHeightWithSpacing()
                    + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeight() * 0.5f };
                ImGui::End(); ImGui::Render();
            };
            const auto prior = volume.getPostProcessProfile();
            const auto original = PostProcessProfileManager::instance().get(prior);
            const auto geometry = volume.settings();
            const auto originalCount = PostProcessProfileManager::instance().getAllHandles().size();
            volume.setPostProcessProfile({});
            frame();
            io.AddMousePosEvent(newButton.x, newButton.y); frame();
            io.AddMouseButtonEvent(0, true); frame();
            io.AddMouseButtonEvent(0, false); frame();
            const auto created = volume.getPostProcessProfile();
            if (created.isValid() && PostProcessProfileManager::instance().getAllHandles().size() > originalCount)
                fixtures.profiles.push_back(created);
            const bool clicked = checks.expect(created.isValid() && created != prior
                && PostProcessProfileManager::instance().getAllHandles().size() == originalCount + 1,
                "Volume New Profile creates an independent asset");
            volume.setPostProcessProfile(prior);
            if (!clicked || !checks.expect(volume.settings() == geometry && PostProcessProfileManager::instance().get(prior) == original,
                "Volume Profile button preserves geometry and prior shared asset"))
                return false;
            for (const auto shape : { PostProcessVolumeShape::Global, PostProcessVolumeShape::Box, PostProcessVolumeShape::Sphere })
            {
                auto selected = geometry;
                selected.shape = shape;
                if (!volume.setSettings(selected)) return false;
                frame();
                if (!checks.expect(ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0
                    && volume.settings() == selected, "Global/Box/Sphere Inspector preserves settings"))
                    return false;
            }
            return volume.setSettings(geometry);
        }
    }

    bool runPostProcessVolumeValidation(DX12Renderer& renderer, const std::filesystem::path& directory,
        const PostProcessValidationRender& render, const PostProcessValidationVerifySaved& verifySaved,
        const PostProcessEffectHandle customEffect)
    {
        Fixture fixtures;
        Checks checks;
        if (!fixtures.initialize())
            return false;
        auto& scenes = SceneManager::instance();
        auto& profiles = PostProcessProfileManager::instance();
        auto* scene = scenes.createScene("Volume validation");
        if (!scene || !scenes.setActiveScene(scene))
            return false;
        auto* cameraObject = scene->createGameObject("Camera", { 1, 1 });
        auto* camera = cameraObject ? cameraObject->addComponent<CameraComponent>() : nullptr;
        auto* object = scene->createGameObject("Volume", { 2, 1 });
        auto* volume = object ? object->addComponent<PostProcessVolumeComponent>() : nullptr;
        if (!camera || !volume)
            return false;
        camera->setPostProcessEnabled(true);
        PostProcessProfile profile;
        profile.name = "Volume exposure";
        profile.settings.exposureEnabled = { true, true };
        profile.settings.exposureEV = { 4, true };
        const auto handle = profiles.create(profile);
        fixtures.profiles.push_back(handle);
        if (!checks.expect(handle.isValid() && volume->setPostProcessProfile(handle), "Volume Profile assignment"))
            return false;
        PostProcessVolumeSettings geometry;
        geometry.weight = 0.5f;
        if (!volume->setSettings(geometry) || !checks.close(volume->influence(Vector3(100, 100, 100)), 0.5f, "Global influence"))
            return false;
        auto snapshot = camera->buildRenderView().postProcess;
        if (!checks.close(snapshot.settings.exposureEV.value, 2, "Volume numeric interpolation")
            || !checks.close(snapshot.effects.exposure, 0.5f, "Volume continuous Enabled")
            || !checks.expect(snapshot.settings.exposureEnabled.value && !snapshot.settings.exposureEV.overrideState,
                "Resolved snapshot strips authoring flags"))
            return false;

        PostProcessProfile customVolumeProfile;
        customVolumeProfile.name = "Volume custom Effect";
        PostProcessEffectInstance customEffectInstance;
        customEffectInstance.effect = customEffect;
        customEffectInstance.parameters[0] = 2;
        customVolumeProfile.customEffects.push_back(customEffectInstance);
        const auto customVolumeHandle = profiles.create(customVolumeProfile);
        fixtures.profiles.push_back(customVolumeHandle);
        if (!checks.expect(customVolumeHandle.isValid()
            && volume->setPostProcessProfile(customVolumeHandle), "Custom Effect Volume Profile assignment"))
            return false;
        snapshot = camera->buildRenderView().postProcess;
        if (!checks.expect(snapshot.customEffects.size() == 1
            && snapshot.customEffects.front().contribution == 0.5f
            && snapshot.customEffects.front().parameters[0] == 2,
            "Volume custom Effect inherits spatial Weight"))
            return false;
        camera->setPostProcessWeight(0.5f);
        snapshot = camera->buildRenderView().postProcess;
        if (!checks.expect(snapshot.weight == 0.5f && snapshot.customEffects.size() == 1
            && snapshot.customEffects.front().contribution == 0.5f,
            "Camera Weight remains separate from custom Volume contribution"))
            return false;
        camera->setPostProcessWeight(1);
        volume->setPostProcessProfile(handle);
        geometry.shape = PostProcessVolumeShape::Box;
        geometry.boxSize = Vector3(2, 4, 6);
        geometry.weight = 1; geometry.blendDistance = 2;
        if (!volume->setSettings(geometry)) return false;
        for (const auto& [point, influence] : std::array{
            std::pair{ Vector3::Zero, 1.0f }, std::pair{ Vector3(1, 2, 3), 1.0f },
            std::pair{ Vector3(2, 0, 0), 0.5f }, std::pair{ Vector3(3, 0, 0), 0.0f },
            std::pair{ Vector3(2, 3, 3), 0.2071068f } })
            if (!checks.close(volume->influence(point), influence, "Box face/corner shell")) return false;
        object->getTransform()->setPosition(Vector3(10, 20, 30));
        object->getTransform()->setRotation(Quaternion::CreateFromAxisAngle(Vector3::UnitZ, DirectX::XM_PIDIV2));
        object->getTransform()->setScale(Vector3(-2, 3, 4));
        geometry.center = Vector3(1, 0, 0);
        if (!volume->setSettings(geometry)) return false;
        const auto center = object->getWorldTransform().transformPoint(geometry.center);
        if (!checks.close(volume->influence(center), 1, "Transformed Box center")
            || !checks.close(volume->influence(center + Vector3(0, 3, 0)), 0.5f, "Rotated mirrored Box world shell"))
            return false;
        geometry.shape = PostProcessVolumeShape::Sphere;
        geometry.sphereRadius = 1;
        if (!volume->setSettings(geometry)
            || !checks.close(volume->influence(center + Vector3(4, 0, 0)), 1, "Sphere max-axis scale")
            || !checks.close(volume->influence(center + Vector3(5, 0, 0)), 0.5f, "Sphere outer shell")
            || !checks.close(volume->influence(center + Vector3(6, 0, 0)), 0, "Sphere shell exterior"))
            return false;
        auto* parent = scene->createGameObject("Parent");
        parent->getTransform()->setPosition(Vector3(5, 0, 0));
        if (!object->setParent(parent, false) || !checks.close(volume->influence(center + Vector3(5, 0, 0)), 1, "Parent Transform"))
            return false;
        object->setParent(nullptr, false);
        object->getTransform()->setPosition(Vector3::Zero);
        object->getTransform()->setRotation(Quaternion::Identity);
        object->getTransform()->setScale(1);
        geometry = {}; geometry.shape = PostProcessVolumeShape::Sphere;
        geometry.sphereRadius = 1; geometry.blendDistance = 0;
        if (!volume->setSettings(geometry)
            || !checks.close(volume->influence(Vector3(1, 0, 0)), 1, "Hard boundary includes surface")
            || !checks.close(volume->influence(Vector3(1.001f, 0, 0)), 0, "Hard boundary exterior"))
            return false;
        LOG_INFO("[VolumeValidation] Begin expected invalid inputs.");
        const auto expectInvalid = [&](const PostProcessVolumeSettings& invalid)
        {
            return checks.expect(!volume->setSettings(invalid) && volume->settings() == geometry,
                "Invalid Volume setter preserves geometry");
        };
        auto invalid = geometry; invalid.weight = -0.1f; if (!expectInvalid(invalid)) return false;
        invalid = geometry; invalid.weight = 1.01f; if (!expectInvalid(invalid)) return false;
        invalid = geometry; invalid.priority = INFINITY; if (!expectInvalid(invalid)) return false;
        invalid = geometry; invalid.center.x = NAN; if (!expectInvalid(invalid)) return false;
        invalid = geometry; invalid.boxSize.y = 0; if (!expectInvalid(invalid)) return false;
        invalid = geometry; invalid.sphereRadius = -1; if (!expectInvalid(invalid)) return false;
        invalid = geometry; invalid.blendDistance = NAN; if (!expectInvalid(invalid)) return false;
        invalid = geometry; invalid.shape = static_cast<PostProcessVolumeShape>(99); if (!expectInvalid(invalid)) return false;
        object->getTransform()->setScale(0);
        if (!checks.close(volume->influence(Vector3::Zero), 0, "Degenerate scale skipped")) return false;
        object->getTransform()->setScale(1);
        if (!checks.close(volume->influence(Vector3::Zero), 1, "Transform recovery")) return false;
        if (!checks.expect(!volume->setPostProcessProfile({ 999999, 99 }) && volume->getPostProcessProfile() == handle,
            "Stale Profile setter retains assignment")) return false;
        LOG_INFO("[VolumeValidation] End expected invalid inputs.");
        geometry = {}; geometry.weight = 1;
        volume->setSettings(geometry);
        object->setLayer(3);
        camera->setPostProcessVolumeMask(1u << 2);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 0, "Volume layer excluded"))
            return false;
        camera->setPostProcessVolumeMask(1u << 3); camera->setCullingMask(0);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 4, "Volume mask independent of geometry"))
            return false;
        volume->setEnabled(false);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 0, "Disabled Volume excluded"))
            return false;
        volume->setEnabled(true); object->setActive(false);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 0, "Inactive Volume excluded"))
            return false;
        object->setActive(true);
        if (!object->setParent(parent, false)) return false;
        parent->setActive(false);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 0, "Inactive parent excluded"))
            return false;
        parent->setActive(true); object->setParent(nullptr, false);
        camera->setPostProcessVolumeMask(UINT32_MAX);
        auto* higherObject = scene->createGameObject("Higher", { 2, 2 });
        auto* higher = higherObject->addComponent<PostProcessVolumeComponent>();
        PostProcessProfile highProfile = profile; highProfile.name = "Higher"; highProfile.settings.exposureEV.value = -4;
        const auto highHandle = profiles.create(highProfile); fixtures.profiles.push_back(highHandle);
        higher->setPostProcessProfile(highHandle);
        PostProcessVolumeSettings highGeometry; highGeometry.weight = 0.5f;
        higher->setSettings(highGeometry);
        PostProcessProfile orderedBaseProfile = profile;
        orderedBaseProfile.name = "Ordered custom Effect";
        orderedBaseProfile.customEffects.push_back(customEffectInstance);
        const auto orderedBaseHandle = profiles.create(orderedBaseProfile);
        fixtures.profiles.push_back(orderedBaseHandle);
        PostProcessProfile orderedHighProfile = highProfile;
        orderedHighProfile.name = "Ordered higher custom Effect";
        auto higherEffect = customEffectInstance;
        higherEffect.parameters[0] = 3;
        orderedHighProfile.customEffects.push_back(higherEffect);
        const auto orderedHighHandle = profiles.create(orderedHighProfile);
        fixtures.profiles.push_back(orderedHighHandle);
        volume->setPostProcessProfile(orderedBaseHandle);
        higher->setPostProcessProfile(orderedHighHandle);
        snapshot = camera->buildRenderView().postProcess;
        const bool volumeGuidFirst = std::tie(object->getGUID().high, object->getGUID().low)
            < std::tie(higherObject->getGUID().high, higherObject->getGUID().low);
        if (!checks.close(snapshot.settings.exposureEV.value, 0, "Equal Priority GUID order")
            || !checks.expect(snapshot.customEffects.size() == 2
                && snapshot.customEffects[0].parameters[0] == (volumeGuidFirst ? 2.0f : 3.0f)
                && snapshot.customEffects[1].parameters[0] == (volumeGuidFirst ? 3.0f : 2.0f),
                "Custom Effect passes follow stable Volume priority/GUID order"))
            return false;
        highGeometry.priority = -1; higher->setSettings(highGeometry);
        snapshot = camera->buildRenderView().postProcess;
        if (!checks.close(snapshot.settings.exposureEV.value, 4, "Priority overrides GUID order")
            || !checks.expect(snapshot.customEffects.size() == 2
                && snapshot.customEffects[0].parameters[0] == 3
                && snapshot.customEffects[1].parameters[0] == 2,
                "Custom Effect passes follow Volume Priority"))
            return false;
        volume->setPostProcessProfile(handle);
        higher->setPostProcessProfile(highHandle);
        highGeometry.priority = 1; higher->setSettings(highGeometry);
        highProfile.settings.exposureEnabled.value = false; profiles.update(highHandle, highProfile);
        snapshot = camera->buildRenderView().postProcess;
        if (!checks.close(snapshot.effects.exposure, 0.5f, "Higher Volume continuously disables effect")
            || !checks.close(snapshot.settings.exposureEV.value, 0, "Higher Volume numeric overrides"))
            return false;
        higher->setEnabled(false);
        PostProcessProfile cameraProfile = highProfile; cameraProfile.name = "Camera Profile";
        cameraProfile.settings.exposureEnabled.value = true; cameraProfile.settings.exposureEV.value = 2;
        const auto cameraHandle = profiles.create(cameraProfile); fixtures.profiles.push_back(cameraHandle);
        camera->setPostProcessProfile(cameraHandle);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 2, "Camera Profile wins over Volumes"))
            return false;
        PostProcessSettings overrides; overrides.exposureEV = { -2, true };
        camera->setPostProcessOverrides(overrides);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, -2, "Camera Overrides win"))
            return false;
        camera->setPostProcessProfile({}); camera->setPostProcessOverrides({});
        camera->setPostProcessEnabled(false);
        if (!checks.expect(!camera->buildRenderView().postProcess.enabled
            && camera->buildRenderView().postProcess.settings.exposureEV.value == 0, "Camera opt-in gates Volumes"))
            return false;
        camera->setPostProcessEnabled(true);
        camera->setPostProcessWeight(0);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 0, "Zero Camera weight skips Volumes"))
            return false;
        camera->setPostProcessWeight(1);
        auto* unusedScene = scenes.createScene("Inactive Volume scene");
        auto* unusedObject = unusedScene->createGameObject("Ignored");
        auto* unusedVolume = unusedObject->addComponent<PostProcessVolumeComponent>();
        unusedVolume->setPostProcessProfile(highHandle);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 4, "Inactive scene excluded"))
            return false;
        auto* pendingObject = scene->createGameObject("Pending Volume");
        auto* pendingVolume = pendingObject->addComponent<PostProcessVolumeComponent>();
        pendingVolume->setPostProcessProfile(highHandle);
        PostProcessVolumeSettings pendingSettings; pendingSettings.priority = 100;
        pendingVolume->setSettings(pendingSettings);
        scene->destroyGameObject(pendingObject);
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 4, "Queued destruction excludes Volume"))
            return false;
        scene->processDestroyQueue();
        if (!checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 4, "Destroyed Volume has no stale registration"))
            return false;
        std::vector<PostProcessVolumeLayer> workspace;
        const std::array<const Scene*, 3> duplicateScenes{ scene, scene, nullptr };
        PostProcessCameraSnapshot duplicateResolution;
        geometry.weight = 0.5f; volume->setSettings(geometry);
        if (!checks.expect(resolvePostProcessVolumes(duplicateResolution, Vector3::Zero, UINT32_MAX, duplicateScenes, workspace),
            "Duplicate-scene resolution succeeds")
            || !checks.close(duplicateResolution.settings.exposureEV.value, 2, "Duplicate scene is not blended twice"))
            return false;
        const auto capacity = workspace.capacity();
        for (unsigned int frame = 0; frame < 100; ++frame)
        {
            PostProcessCameraSnapshot next;
            if (!resolvePostProcessVolumes(next, Vector3::Zero, UINT32_MAX, duplicateScenes, workspace))
                return false;
        }
        if (!checks.expect(workspace.empty() && workspace.capacity() == capacity, "Volume workspace reuses capacity without retained references"))
            return false;
        const auto beforeInvalidPosition = duplicateResolution;
        if (!checks.expect(!resolvePostProcessVolumes(duplicateResolution, Vector3(NAN, 0, 0), UINT32_MAX, duplicateScenes, workspace)
            && duplicateResolution == beforeInvalidPosition, "Invalid Camera position preserves destination"))
            return false;
        geometry.weight = 1; volume->setSettings(geometry);
        auto* persistentObject = scenes.getPersistentScene()->createGameObject("Persistent", { 2, 3 });
        auto* persistent = persistentObject->addComponent<PostProcessVolumeComponent>();
        persistent->setPostProcessProfile(highHandle);
        highGeometry.weight = 1; persistent->setSettings(highGeometry);
        if (!checks.expect(!camera->buildRenderView().postProcess.settings.exposureEnabled.value, "Persistent Volume participates"))
            return false;
        persistent->setEnabled(false);
        const auto frozen = camera->buildRenderView();
        CameraRenderSubmissionQueue::instance().submit(frozen);
        auto updated = *profiles.get(handle); updated.settings.exposureEV.value = 1; profiles.update(handle, updated);
        RenderView queued;
        if (!checks.expect(CameraRenderSubmissionQueue::instance().consume(queued) && queued.postProcess == frozen.postProcess,
            "Volume Profile edits do not mutate queued snapshot")) return false;
        updated.settings.exposureEV.value = 4; profiles.update(handle, updated);
        std::vector<std::uint8_t> saved{ 42 }, unchanged{ 42 };
        if (!checks.expect(!volume->serializePayload(saved) && saved == unchanged, "Unsaved Profile payload preserves output"))
            return false;
        PostProcessVolumeComponent clone;
        if (!checks.expect(volume->copyConfigurationTo(clone) && clone.getPostProcessProfile() == handle
            && clone.settings() == volume->settings(), "Unsaved Volume clone"))
            return false;
        const auto profilePath = directory / L"volume-profile.postprofile";
        if (!profiles.save(handle, profilePath) || !profiles.save(highHandle, directory / L"volume-higher.postprofile")
            || !volume->serializePayload(saved)) return false;
        PostProcessVolumeComponent restored;
        if (!checks.expect(restored.deserializePayload(1, saved) && restored.settings() == geometry
            && restored.getPostProcessProfile() == handle, "Volume payload round trip"))
            return false;
        for (const auto& bad : { std::vector<std::uint8_t>{ 0, 1, 2 }, payload(invalid), payload(geometry, {}, "missing.postprofile"),
            payload(geometry, profiles.get(handle)->guid, "", false), payload(geometry, {}, "", true) })
            if (!checks.expect(!restored.deserializePayload(1, bad) && restored.settings() == geometry
                && restored.getPostProcessProfile() == handle, "Malformed payload retains geometry and Profile")) return false;
        if (!checks.expect(!restored.deserializePayload(99, saved), "Unsupported Volume version"))
            return false;
        const auto scenePath = directory / L"volume-scene.scene";
        camera->setPostProcessVolumeMask(1u << 3);
        if (!Serialization::SceneSerializer{}.save(scenePath, *scene)) return false;
        Scene loaded(ObjectGUID::generate(), "Loaded"), cloned(ObjectGUID::generate(), "Cloned");
        if (!Serialization::SceneSerializer{}.load(scenePath, loaded)) return false;
        const std::array pairs{ SceneClonePair{ *scene, cloned } };
        if (!cloneScenes(pairs)) return false;
        for (auto* target : { &loaded, &cloned })
        {
            auto* v = target->find(object->getGUID())->getComponent<PostProcessVolumeComponent>();
            auto* c = target->find(cameraObject->getGUID())->getComponent<CameraComponent>();
            if (!checks.expect(v && c && v->settings() == geometry && v->getPostProcessProfile() == handle
                && c->getPostProcessVolumeMask() == (1u << 3), "Scene load/Play clone preserve Volume and Camera mask")) return false;
        }
        Serialization::FlatBufferReader sceneBefore;
        if (!sceneBefore.open(scenePath)) return false;
        const std::vector<std::uint8_t> sceneBytes(sceneBefore.data(), sceneBefore.data() + sceneBefore.size());
        PostProcessProfile unsavedProfile = profile;
        unsavedProfile.name = "Unsaved reference";
        const auto unsaved = profiles.create(unsavedProfile); fixtures.profiles.push_back(unsaved);
        volume->setPostProcessProfile(unsaved);
        if (!checks.expect(!Serialization::SceneSerializer{}.save(scenePath, *scene), "Unsaved Volume reference rejects Scene save"))
            return false;
        Serialization::FlatBufferReader sceneAfter;
        if (!sceneAfter.open(scenePath) || !checks.expect(sceneBytes == std::vector<std::uint8_t>(sceneAfter.data(),
            sceneAfter.data() + sceneAfter.size()), "Failed Scene save preserves existing file")) return false;
        const std::array unsavedPairs{ SceneClonePair{ *scene, loaded } };
        loaded.clear();
        if (!checks.expect(cloneScenes(unsavedPairs)
            && loaded.find(object->getGUID())->getComponent<PostProcessVolumeComponent>()->getPostProcessProfile() == unsaved,
            "Play scene clone supports unsaved Volume Profiles"))
            return false;
        volume->setPostProcessProfile(handle);
        Prefab prefab;
        if (!prefab.capture(*object) || !Serialization::PrefabSerializer{}.save(directory / L"volume.prefab", prefab))
            return false;
        Prefab loadedPrefab;
        if (!Serialization::PrefabSerializer{}.load(directory / L"volume.prefab", loadedPrefab)) return false;
        Scene instantiated(ObjectGUID::generate(), "Instantiated");
        auto* root = loadedPrefab.instantiate(instantiated);
        if (!checks.expect(root && root->getComponent<PostProcessVolumeComponent>()
            && root->getComponent<PostProcessVolumeComponent>()->settings() == geometry, "Prefab Volume round trip")) return false;
        std::vector<std::uint8_t> cameraPayload;
        if (!camera->serializePayload(cameraPayload)) return false;
        CameraComponent legacy;
        if (!checks.expect(legacy.deserializePayload(2, cameraPayload) && legacy.getPostProcessVolumeMask() == UINT32_MAX,
                "Camera v2 defaults Volume mask")
            || !checks.expect(legacy.deserializePayload(3, cameraPayload) && legacy.getPostProcessVolumeMask() == (1u << 3),
                "Camera v3 restores Volume mask")
            || !checks.expect(legacy.deserializePayload(0, {}) && legacy.getPostProcessVolumeMask() == UINT32_MAX,
                "Camera v0 resets Volume mask")) return false;
        auto unicodeProfile = profile; unicodeProfile.name = "Unicode Volume";
        const auto unicodeHandle = profiles.create(unicodeProfile); fixtures.profiles.push_back(unicodeHandle);
        const auto unicodePath = directory / L"volume-\u65e5\u672c\u8a9e.postprofile";
        if (!profiles.save(unicodeHandle, unicodePath) || !volume->setPostProcessProfile(unicodeHandle)
            || !volume->serializePayload(saved)) return false;
        const auto unicodeGuid = profiles.get(unicodeHandle)->guid;
        profiles.unload(unicodeHandle);
        if (!checks.expect(!volume->profileSnapshot(), "Stale Volume Profile contributes nothing")
            || !checks.close(camera->buildRenderView().postProcess.settings.exposureEV.value, 0, "Stale Profile leaves neutral settings"))
            return false;
        if (!restored.deserializePayload(1, saved)) return false;
        const auto coldHandle = restored.getPostProcessProfile(); fixtures.profiles.push_back(coldHandle);
        if (!checks.expect(profiles.get(coldHandle)->guid == unicodeGuid && coldHandle != unicodeHandle,
            "Unicode Volume cold load preserves asset identity with fresh generation")) return false;
        const auto unicodeText = String::fromWide(unicodePath.c_str());
        const auto wrongGuid = ObjectGUID::generate();
        const auto invalidReference = payload(geometry, wrongGuid, std::string(unicodeText.c_str(), unicodeText.size()));
        if (!checks.expect(!restored.deserializePayload(1, invalidReference)
            && restored.getPostProcessProfile() == coldHandle && restored.settings() == geometry,
            "Mismatched GUID rejects Volume restoration without mutation")) return false;
        volume->setPostProcessProfile(handle);
        PostProcessSettings allValues;
        for (const auto& descriptor : POST_PROCESS_FLOATS)
            allValues.*descriptor.member = { descriptor.maximum, true };
        for (const auto& descriptor : POST_PROCESS_COLORS)
            allValues.*descriptor.member = { Vector3(descriptor.maximum, descriptor.maximum, descriptor.maximum), true };
        for (const auto& descriptor : POST_PROCESS_SWITCHES)
            allValues.*descriptor.member = { true, true };
        allValues.colorGradingLutAsset.value = ObjectGUID::generate();
        PostProcessCameraSnapshot allBlended;
        blendPostProcessLayer(allBlended, allValues, 0.5f);
        const PostProcessSettings neutral;
        for (const auto& descriptor : POST_PROCESS_FLOATS)
            if (!checks.close((allBlended.settings.*descriptor.member).value,
                std::lerp((neutral.*descriptor.member).value, descriptor.maximum, 0.5f), "All scalar descriptors blend"))
                return false;
        for (const auto& descriptor : POST_PROCESS_COLORS)
            if (!checks.expect((allBlended.settings.*descriptor.member).value
                == Vector3::Lerp((neutral.*descriptor.member).value,
                    Vector3(descriptor.maximum, descriptor.maximum, descriptor.maximum), 0.5f), "All color descriptors blend"))
                return false;
        for (const auto member : POST_PROCESS_WEIGHT_MEMBERS)
            if (!checks.close(allBlended.effects.*member, 0.5f, "All switch descriptors blend continuously")) return false;
        const auto allFrozen = allBlended;
        blendPostProcessLayer(allBlended, PostProcessSettings{}, 1);
        if (!checks.expect(allFrozen == allBlended, "Unchecked overrides leave inherited values and contributions intact"))
            return false;
        blendPostProcessLayer(allBlended, allValues, 0);
        if (!checks.expect(allFrozen == allBlended, "Zero Volume layer is a strict no-op")) return false;
        geometry = {}; volume->setSettings(geometry);
        if (!inspector(checks, *volume, fixtures)) return false;
        camera->setPostProcessVolumeMask(UINT32_MAX);
        camera->setCullingMask(UINT32_MAX);
        camera->setPostProcessWeight(1);
        std::size_t gpuCases = 0;
        if (!renderer.resize(320, 180) || !renderer.setPostProcessPasses({})) return false;
        for (const auto shape : { PostProcessVolumeShape::Global, PostProcessVolumeShape::Box, PostProcessVolumeShape::Sphere })
        {
            geometry = {}; geometry.shape = shape; geometry.boxSize = Vector3(2, 2, 2);
            geometry.sphereRadius = 1; geometry.blendDistance = 2;
            if (!volume->setSettings(geometry)) return false;
            for (const auto& [x, contribution] : std::array{
                std::pair{ 0.0f, 1.0f }, std::pair{ 1.0f, 1.0f }, std::pair{ 2.0f, 0.5f }, std::pair{ 3.0f, 0.0f } })
            {
                cameraObject->getTransform()->setPosition(Vector3(x, 0, 0));
                const float alpha = shape == PostProcessVolumeShape::Global ? 1 : contribution;
                const auto actual = camera->buildRenderView().postProcess;
                if (!checks.close(actual.settings.exposureEV.value, 4 * alpha, "Moving Camera Volume EV")
                    || !checks.close(actual.settings.exposureEnabled.value ? actual.effects.exposure : 0, alpha, "Moving Camera enable contribution"))
                    return false;
                PostProcessCameraSnapshot expected;
                expected.enabled = true; expected.settings.exposureEnabled.value = alpha > 0;
                expected.settings.exposureEV.value = 4 * alpha; expected.effects.exposure = alpha > 0 ? alpha : 1;
                DX12FrameCapture reference, captured;
                if (!render(320, 180, {}, expected, PostProcessValidationFixture::Panels) || !renderer.captureFrame(reference)
                    || !render(320, 180, {}, actual, PostProcessValidationFixture::Panels) || !renderer.captureFrame(captured)
                    || !identical(reference.display, captured.display) || !identical(reference.hdr, captured.hdr)
                    || !renderer.validateDebugMessages())
                { LOG_ERROR("[VolumeValidation] Moving Camera GPU image mismatch."); return false; }
                const std::wstring name = L"volume-" + std::to_wstring(static_cast<unsigned int>(shape)) + L"-" + std::to_wstring(gpuCases++);
                if (!captured.save(directory, name) || !verifySaved(captured, directory, name)) return false;
            }
        }
        PostProcessProfile combined;
        combined.name = "Combined Volume";
        combined.settings.bloomEnabled = { true, true };
        combined.settings.bloomIntensity = { 1, true };
        combined.settings.exposureEnabled = { true, true };
        combined.settings.exposureEV = { 2, true };
        combined.settings.colorAdjustmentsEnabled = { true, true };
        combined.settings.contrast = { 30, true };
        combined.settings.saturation = { -40, true };
        combined.settings.colorFilter = { Vector3(0.8f, 1.2f, 0.6f), true };
        combined.settings.vignetteEnabled = { true, true };
        combined.settings.vignetteIntensity = { 0.6f, true };
        combined.settings.vignetteColor = { Vector3(0.1f, 0.2f, 0.3f), true };
        combined.settings.fxaaEnabled = { true, true };
        const auto combinedHandle = profiles.create(combined); fixtures.profiles.push_back(combinedHandle);
        if (!volume->setPostProcessProfile(combinedHandle)) return false;
        geometry = {}; geometry.weight = 0.5f;
        if (!volume->setSettings(geometry)) return false;
        camera->setPostProcessWeight(0.5f);
        const auto combinedSnapshot = camera->buildRenderView().postProcess;
        PostProcessCameraSnapshot combinedExpected;
        combinedExpected.enabled = true; combinedExpected.weight = 0.5f;
        combinedExpected.effects = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
        combinedExpected.settings.bloomEnabled.value = true; combinedExpected.settings.bloomIntensity.value = 0.5f;
        combinedExpected.settings.exposureEnabled.value = true; combinedExpected.settings.exposureEV.value = 1;
        combinedExpected.settings.colorAdjustmentsEnabled.value = true;
        combinedExpected.settings.contrast.value = 15; combinedExpected.settings.saturation.value = -20;
        combinedExpected.settings.colorFilter.value = Vector3::Lerp(Vector3::One, combined.settings.colorFilter.value, 0.5f);
        combinedExpected.settings.vignetteEnabled.value = true; combinedExpected.settings.vignetteIntensity.value = 0.3f;
        combinedExpected.settings.vignetteColor.value = Vector3(0.05f, 0.1f, 0.15f);
        combinedExpected.settings.fxaaEnabled.value = true;
        combinedExpected.cameraGuid = combinedSnapshot.cameraGuid;
        combinedExpected.deltaTimeSeconds = combinedSnapshot.deltaTimeSeconds;
        combinedExpected.nearClip = combinedSnapshot.nearClip;
        combinedExpected.farClip = combinedSnapshot.farClip;
        combinedExpected.perspectiveProjection = combinedSnapshot.perspectiveProjection;
        if (!checks.expect(combinedExpected == combinedSnapshot, "Combined Volume snapshot matches independent interpolation"))
            return false;
        DX12FrameCapture combinedReference, combinedCapture;
        if (!render(320, 180, {}, combinedExpected, PostProcessValidationFixture::Diagonal)
            || !renderer.captureFrame(combinedReference)
            || !render(320, 180, {}, combinedSnapshot, PostProcessValidationFixture::Diagonal)
            || !renderer.captureFrame(combinedCapture)
            || !checks.expect(identical(combinedCapture.display, combinedReference.display)
                && identical(combinedCapture.hdr, combinedReference.hdr), "Combined Volume and Camera Weight GPU image"))
            return false;
        if (!combinedCapture.save(directory, L"volume-combined")
            || !verifySaved(combinedCapture, directory, L"volume-combined") || !renderer.validateDebugMessages()) return false;
        ++gpuCases;
        std::ofstream report(directory / L"volume-results.json", std::ios::binary);
        report << "{\n  \"phase\": 4,\n  \"checks\": " << checks.count << ",\n  \"gpuCases\": " << gpuCases
            << ",\n  \"debugWarningsAndErrors\": 0,\n  \"savedImagesMatchReadback\": true,\n  \"passed\": true\n}\n";
        report.close();
        if (!report) { LOG_ERROR("[VolumeValidation] Cannot save Volume results."); return false; }
        LOG_INFO("[VolumeValidation] {} CPU/authoring checks and {} Camera GPU cases passed.", checks.count, gpuCases);
        return true;
    }
}
