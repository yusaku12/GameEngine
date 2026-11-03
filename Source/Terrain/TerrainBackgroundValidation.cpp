#include "Pch.h"
#include "Terrain\TerrainBackgroundValidation.h"
#include "Core\GameObject\Component\TerrainComponent.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Threading\JobSystem.h"
#include "Core\Threading\ThreadUtility.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Assets\Model\ModelManager.h"
#include "Physics\Collider\TerrainColliderComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\Camera\CameraUtils.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Graphics\DirectX12\FrameCapture.h"
#include "Graphics\Lighting\LightingFrame.h"
#include <fstream>
#include <wincodec.h>

namespace Engine
{
    namespace
    {
        struct Checks
        {
            std::size_t count = 0;
            bool expect(const bool value, const char* label)
            {
                if (!value)
                    LOG_ERROR("[TerrainBackgroundValidation] FAILED: {}.", label);
                else
                    ++count;
                return value;
            }
        };

        bool awaitReady(Checks& checks, const TerrainComponent& terrain)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (terrain.getGenerationStatus().pending()
                && terrain.getGenerationStatus().stage != TerrainGenerationStage::Ready
                && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return checks.expect(terrain.getGenerationStatus().stage == TerrainGenerationStage::Ready,
                "worker reaches Ready without any owner-thread completion processing");
        }

        bool awaitApplied(Checks& checks, const TerrainComponent& terrain)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (terrain.getGenerationStatus().pending() && std::chrono::steady_clock::now() < deadline)
            {
                TerrainGenerationQueue::instance().applyCompleted();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto status = terrain.getGenerationStatus();
            return checks.expect(status.stage == TerrainGenerationStage::Applied
                && status.workerThread != 0 && status.workerThread != getCurrentThreadId()
                && status.applyThread == getCurrentThreadId(), "CPU build runs off-thread; application runs only on caller's frame barrier");
        }

        bool equivalent(Checks& checks, const TerrainComponent& a, const TerrainComponent& b)
        {
            std::vector<std::uint8_t> left, right;
            if (!checks.expect(a.serializePayload(left) && b.serializePayload(right) && left == right,
                "async and sync applied payloads are byte-identical"))
                return false;
            const auto x = ModelManager::instance().get(a.getModel()), y = ModelManager::instance().get(b.getModel());
            if (!checks.expect(x && y && x->meshes.size() == y->meshes.size(), "async/sync model chunk counts"))
                return false;
            for (std::size_t index = 0; index < x->meshes.size(); ++index)
            {
                const auto& p = x->meshes[index];
                const auto& q = y->meshes[index];
                if (!checks.expect(p.indices == q.indices && p.vertices.size() == q.vertices.size()
                    && Vector3(p.boundingBox.Center) == Vector3(q.boundingBox.Center)
                    && Vector3(p.boundingBox.Extents) == Vector3(q.boundingBox.Extents),
                    "async/sync exact full-resolution and LOD indices/bounds"))
                    return false;
                for (std::size_t vertex = 0; vertex < p.vertices.size(); ++vertex)
                {
                    const auto& u = p.vertices[vertex];
                    const auto& v = q.vertices[vertex];
                    if (!checks.expect(u.position == v.position && u.normal == v.normal && u.tangent == v.tangent
                        && u.bitangent == v.bitangent && u.texCoord == v.texCoord && u.color == v.color,
                        "async/sync every vertex attribute is exact"))
                        return false;
                }
                const auto& l = a.getLodSnapshot()->chunks[index];
                const auto& r = b.getLodSnapshot()->chunks[index];
                if (!checks.expect(l.levelCount == r.levelCount, "async/sync available LOD levels"))
                    return false;
                for (std::uint32_t level = 0; level < l.levelCount; ++level)
                    if (!checks.expect(l.levels[level].start == r.levels[level].start && l.levels[level].count == r.levels[level].count,
                        "async/sync every immutable LOD range"))
                        return false;
            }
            return true;
        }

        bool parity(Checks& checks, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "Async parity");
            auto* object = scene.createGameObject("Async terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            TerrainComponent reference;
            TerrainMaterialSettings material;
            material.enabled = true;
            const TerrainMeshSettings mesh{ 32, 24, 5, -1, 3, 5 };
            const TerrainLodSettings lod{ true, 4, 17 };
            if (!checks.expect(terrain->setChunkSettings({ 17 }) && terrain->setMaterialSettings(material)
                && terrain->setLodSettings(lod) && reference.setChunkSettings({ 17 })
                && reference.setMaterialSettings(material) && reference.setLodSettings(lod), "async parity settings fixture"))
                return false;
            scene.update(0);
            TerrainNoiseSettings noise;
            noise.width = 65;
            noise.height = 41;
            noise.seed = 189;
            noise.offsetX = -2.37f;
            const auto oldModel = terrain->getModel();
            const auto oldAsset = collider->getChunkAssets()[0];
            std::vector<std::uint8_t> oldPayload, pendingPayload;
            if (!checks.expect(terrain->serializePayload(oldPayload) && terrain->requestNoise(noise, mesh)
                && terrain->getModel() == oldModel && collider->getChunkAssets()[0] == oldAsset
                && terrain->serializePayload(pendingPayload) && pendingPayload == oldPayload,
                "accepted work leaves model, collision, samples and saved applied data unchanged") || !awaitReady(checks, *terrain))
                return false;
            if (!checks.expect(terrain->getModel() == oldModel && collider->getChunkAssets()[0] == oldAsset
                && Serialization::SceneSerializer{}.save(directory / L"background-pending.scene", scene),
                "Ready CPU results cannot modify the scene before completion barrier"))
                return false;
            TerrainComponent clone;
            if (!checks.expect(terrain->copyConfigurationTo(clone) && clone.getGenerationStatus().stage == TerrainGenerationStage::Idle
                && clone.getHeightmap().width() == 65 && clone.getNoiseSettings().seed == 0,
                "clone copies only applied terrain; does not inherit in-flight tasks"))
                return false;
            if (!awaitApplied(checks, *terrain) || !checks.expect(reference.generateNoise(noise, mesh), "sync noise oracle")
                || !equivalent(checks, *terrain, reference))
                return false;
            std::vector<std::shared_ptr<const PhysicsMeshAsset>> cooked;
            if (!checks.expect(cookTerrainChunks(*ModelManager::instance().get(reference.getModel()), cooked)
                && cooked.size() == collider->getChunkAssets().size(), "async cooking oracle"))
                return false;
            for (std::size_t index = 0; index < cooked.size(); ++index)
                if (!checks.expect(std::ranges::equal(cooked[index]->getCookedData(), collider->getChunkAssets()[index]->getCookedData()),
                    "worker-cooked PhysX bytes equal synchronous full-resolution-only oracle"))
                    return false;
            auto* world = PhysicsSystem::instance().getWorld();
            for (int z = -10; z <= 10; z += 2)
                for (int x = -14; x <= 14; x += 2)
                {
                    const Vector3 position(x + 0.13f, 20, z + 0.31f);
                    const auto hit = world->raycast(position, -Vector3::UnitY, 40);
                    const auto surface = terrain->sampleWorldSurface(position);
                    if (!checks.expect(hit && surface && hit->collider == collider && std::abs(hit->point.y - surface->position.y) < 0.001f,
                        "async collision application updates actual PhysX queries"))
                        return false;
                }
            Heightmap flat;
            if (!checks.expect(terrain->requestFlat(33, 18, 0.37f, mesh) && flat.createFlat(33, 18, 0.37f)
                && reference.setTerrain(flat, mesh), "queue flat creation") || !awaitApplied(checks, *terrain)
                || !equivalent(checks, *terrain, reference))
                return false;
            Heightmap imported;
            if (!checks.expect(terrain->requestHeightmap(directory / L"height8.png", mesh)
                && imported.loadImage(directory / L"height8.png") && reference.setTerrain(imported, mesh), "queue WIC decoding")
                || !awaitApplied(checks, *terrain) || !equivalent(checks, *terrain, reference))
                return false;
            DirectX::ScratchImage sixteen;
            if (!checks.expect(SUCCEEDED(sixteen.Initialize2D(DXGI_FORMAT_R16_UNORM, 3, 2, 1, 1)), "16-bit background image fixture"))
                return false;
            const auto* pixels = sixteen.GetImage(0, 0, 0);
            const std::array<std::uint16_t, 6> values{ 0, 1, 12345, 32768, 65534, 65535 };
            for (std::size_t row = 0; row < 2; ++row)
                std::memcpy(pixels->pixels + row * pixels->rowPitch, values.data() + row * 3, 3 * sizeof(std::uint16_t));
            const auto imagePath = directory / L"background-height16.png";
            if (!checks.expect(SUCCEEDED(DirectX::SaveToWICFile(*pixels, DirectX::WIC_FLAGS_IGNORE_SRGB,
                DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), imagePath.c_str(), &GUID_WICPixelFormat16bppGray))
                && terrain->requestHeightmap(imagePath, mesh), "queue independent worker WIC COM decode at 16-bit precision")
                || !awaitApplied(checks, *terrain))
                return false;
            for (std::size_t index = 0; index < values.size(); ++index)
                if (!checks.expect(std::abs(terrain->getHeightmap().samples()[index]
                    - static_cast<float>(values[index]) / 65535) < 0.000002f, "worker decode preserves low 16-bit sample values"))
                    return false;
            if (!checks.expect(flat.createFlat(41, 36, 0.19f) && terrain->requestTerrain(flat, mesh)
                && reference.setTerrain(flat, mesh) && flat.createFlat(2, 2, 1), "request copies caller-owned samples"))
                return false;
            if (!awaitApplied(checks, *terrain) || !equivalent(checks, *terrain, reference))
                return false;
            material.layers[0].roughness = 0.31f;
            const TerrainMeshSettings resized{ 41, 36, 8, 2, 7, 11 };
            if (!checks.expect(terrain->requestRebuild(resized, material, { 16 }, { true, 2, 11 })
                && reference.setTerrain(reference.getHeightmap(), resized) && reference.setMaterialSettings(material)
                && reference.setChunkSettings({ 16 }) && reference.setLodSettings({ true, 2, 11 }), "queue all authoring settings")
                || !awaitApplied(checks, *terrain) || !equivalent(checks, *terrain, reference))
                return false;
            Scene restored(ObjectGUID::generate(), "Pending save restored");
            if (!checks.expect(Serialization::SceneSerializer{}.load(directory / L"background-pending.scene", restored),
                "pending save can be loaded independently"))
                return false;
            const auto* saved = restored.find(object->getGUID())->getComponent<TerrainComponent>();
            return checks.expect(saved && saved->serializePayload(pendingPayload) && pendingPayload == oldPayload
                && saved->getGenerationStatus().stage == TerrainGenerationStage::Idle,
                "pending scene save restores only previously applied data, with no background operation");
        }

        bool lifecycle(Checks& checks, const std::filesystem::path& directory)
        {
            {
                Scene fresh(ObjectGUID::generate(), "New async terrain");
                auto* first = fresh.createGameObject("Ungenerated")->addComponent<TerrainComponent>();
                if (!checks.expect(first->requestFlat(33, 33, 0.4f, {}), "request before first automatic render generation"))
                    return false;
                ModelRenderSubmissionQueue::instance().clear();
                fresh.submitRenderData();
                std::vector<ModelRenderSubmission> queue;
                ModelRenderSubmissionQueue::instance().consume(queue);
                if (!checks.expect(first->getGenerationStatus().pending() && !first->getModel().isValid() && queue.empty(),
                    "first submission never triggers synchronous fallback or cancels pending background work")
                    || !awaitApplied(checks, *first))
                    return false;
            }
            {
                TerrainComponent a, b;
                if (!checks.expect(a.requestFlat(17, 17, 0.1f, {}) && b.requestFlat(17, 17, 0.2f, {}),
                    "independent terrains share a bounded single worker")
                    || !awaitReady(checks, a) || !awaitReady(checks, b))
                    return false;
                TerrainGenerationQueue::instance().applyCompleted();
                if (!checks.expect((a.getGenerationStatus().stage == TerrainGenerationStage::Applied)
                    != (b.getGenerationStatus().stage == TerrainGenerationStage::Applied),
                    "one completion pump applies at most one terrain, not a burst of all ready models"))
                    return false;
                TerrainGenerationQueue::instance().applyCompleted();
                if (!checks.expect(a.getGenerationStatus().stage == TerrainGenerationStage::Applied
                    && b.getGenerationStatus().stage == TerrainGenerationStage::Applied
                    && a.getGenerationStatus().workerThread == b.getGenerationStatus().workerThread,
                    "subsequent frame applies next independent result on the same worker"))
                    return false;
            }
            Scene scene(ObjectGUID::generate(), "Async cancellation");
            auto* object = scene.createGameObject("Terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            Heightmap flat;
            if (!checks.expect(flat.createFlat(33, 33, 0.2f) && terrain->setTerrain(flat, { 4, 4, 1, 0, 1, 1 }),
                "cancellation fixture"))
                return false;
            scene.update(0);
            const auto previous = terrain->getModel();
            TerrainNoiseSettings slow;
            slow.width = slow.height = 1025;
            slow.octaves = 12;
            if (!checks.expect(terrain->requestNoise(slow, terrain->getSettings()), "queue slow noise before cancellation"))
                return false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (terrain->getGenerationStatus().stage == TerrainGenerationStage::Queued && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Source,
                "observe active CPU generation before cancellation"))
                return false;
            terrain->cancelGeneration();
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Cancelled
                && terrain->getModel() == previous, "active cancellation immediately prevents application"))
                return false;
            for (std::uint32_t request = 0; request < 40; ++request)
                if (!checks.expect(terrain->requestFlat(33, 33, static_cast<float>(request) / 40, terrain->getSettings()),
                    "latest request supersedes queued and active builds without waiting"))
                    return false;
            if (!awaitApplied(checks, *terrain)
                || !checks.expect(terrain->getHeightmap().samples()[0] == 39.0f / 40, "only latest requested surface becomes live"))
                return false;
            if (!checks.expect(terrain->requestNoise(slow, terrain->getSettings()), "queue work before invalid replacement"))
                return false;
            TerrainNoiseSettings invalid = slow;
            invalid.width = 1;
            if (!checks.expect(!terrain->requestNoise(invalid, terrain->getSettings()) && terrain->getGenerationStatus().pending(),
                "invalid replacement does not cancel valid existing work"))
                return false;
            Heightmap movedFrom;
            const Heightmap moved = std::move(movedFrom);
            if (!checks.expect(!terrain->requestTerrain(movedFrom, terrain->getSettings())
                && terrain->getGenerationStatus().pending() && moved.samples().size() == 4,
                "moved-from invalid input is rejected before superseding valid work"))
                return false;
            if (!checks.expect(flat.createFlat(2, 2, 0.8f) && terrain->setTerrain(flat, terrain->getSettings())
                && terrain->getGenerationStatus().stage == TerrainGenerationStage::Cancelled, "successful synchronous edit supersedes pending work"))
                return false;
            const auto synchronous = terrain->getModel();
            if (!checks.expect(terrain->requestFlat(33, 33, 0.1f, terrain->getSettings()), "queue work for ready cancellation")
                || !awaitReady(checks, *terrain))
                return false;
            terrain->cancelGeneration();
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getModel() == synchronous, "cancellation discards already-ready results"))
                return false;
            if (!checks.expect(terrain->requestFlat(33, 33, 0.6f, terrain->getSettings()), "queue request before invalid synchronous edit")
                || !awaitReady(checks, *terrain))
                return false;
            auto invalidMesh = terrain->getSettings();
            invalidMesh.sizeX = 0;
            if (!checks.expect(!terrain->setTerrain(flat, invalidMesh) && terrain->getGenerationStatus().pending(),
                "failed synchronous edit does not invalidate an accepted asynchronous result")
                || !awaitApplied(checks, *terrain))
                return false;
            const auto stable = terrain->getModel();
            if (!checks.expect(terrain->requestHeightmap(directory / L"missing-background.png", terrain->getSettings()),
                "accept image request without doing I/O on requester"))
                return false;
            const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (terrain->getGenerationStatus().pending() && std::chrono::steady_clock::now() < failureDeadline)
            {
                TerrainGenerationQueue::instance().applyCompleted();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && terrain->getModel() == stable, "worker decode failure is explicit and preserves previous terrain"))
                return false;
            if (!checks.expect(terrain->requestFlat(17, 17, 0.1f, terrain->getSettings()), "queue work before disabling")
                || !awaitReady(checks, *terrain))
                return false;
            terrain->setEnabled(false);
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Cancelled
                && terrain->getModel() == stable, "disabling component prevents late completion"))
                return false;
            terrain->setEnabled(true);
            if (!checks.expect(terrain->requestFlat(17, 17, 0.1f, terrain->getSettings()), "queue ready result before component removal")
                || !awaitReady(checks, *terrain) || !checks.expect(object->removeComponent<TerrainComponent>(), "remove pending terrain"))
                return false;
            auto* replacement = object->addComponent<TerrainComponent>();
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(replacement->getGenerationStatus().stage == TerrainGenerationStage::Idle
                && replacement->getHeightmap().samples()[0] == 0, "removed component's result cannot target a replacement at the same GUID"))
                return false;
            if (!checks.expect(replacement->requestNoise(slow, replacement->getSettings()), "queue source before scene destruction"))
                return false;
            scene.clear();
            auto temporary = std::make_unique<TerrainComponent>();
            if (!checks.expect(temporary->requestNoise(slow, {}), "standalone component destruction fixture"))
                return false;
            temporary.reset();
            TerrainGenerationQueue::instance().shutdown();
            TerrainGenerationQueue::instance().applyCompleted();
            TerrainComponent restarted;
            return checks.expect(restarted.requestFlat(17, 17, 0.7f, {}), "worker restarts after joined shutdown")
                && awaitApplied(checks, restarted);
        }

        bool physicsRollback(Checks& checks)
        {
            Scene scene(ObjectGUID::generate(), "Async physics transaction");
            auto* object = scene.createGameObject("Terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            Heightmap flat;
            if (!checks.expect(flat.createFlat(17, 17, 0.25f) && terrain->setTerrain(flat, { 8, 8, 4, 0, 1, 1 }),
                "async physics rollback fixture"))
                return false;
            scene.update(0);
            auto* world = PhysicsSystem::instance().getWorld();
            const auto previous = terrain->getModel();
            const auto asset = collider->getPhysicsAsset();
            if (!checks.expect(terrain->requestFlat(17, 17, 0.75f, terrain->getSettings()), "queue replacement collision")
                || !awaitReady(checks, *terrain))
                return false;
            world->simulate(1.0f / 60);
            TerrainGenerationQueue::instance().applyCompleted();
            world->fetchResults();
            const auto hit = world->raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20);
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && terrain->getModel() == previous && collider->getPhysicsAsset() == asset
                && hit && std::abs(hit->point.y - 1) < 0.001f, "busy-physics apply rolls back model, assets and live shapes together"))
                return false;
            if (!checks.expect(terrain->requestFlat(17, 17, 0.75f, terrain->getSettings()), "retry after physics completes")
                || !awaitApplied(checks, *terrain))
                return false;
            const auto replacementModel = terrain->getModel();
            if (!checks.expect(terrain->requestFlat(33, 33, 0.9f, terrain->getSettings()), "queue result before collider removal")
                || !awaitReady(checks, *terrain) || !checks.expect(object->removeComponent<TerrainColliderComponent>(), "remove collider"))
                return false;
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && terrain->getModel() == replacementModel, "collider presence changes reject obsolete collision contracts"))
                return false;
            if (!checks.expect(terrain->requestFlat(33, 33, 0.9f, terrain->getSettings()), "queue result without collider")
                || !awaitReady(checks, *terrain) || !checks.expect(object->addComponent<TerrainColliderComponent>() != nullptr, "add late collider"))
                return false;
            TerrainGenerationQueue::instance().applyCompleted();
            return checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && terrain->getModel() == replacementModel, "late-added collider never causes CPU cooking on completion thread");
        }

        RenderView overview()
        {
            RenderView view;
            view.camera.position = Vector3(0, 20, 0);
            view.camera.forward = -Vector3::UnitY;
            view.camera.view = Matrix(DirectX::XMMatrixLookAtLH(view.camera.position, Vector3::Zero, Vector3::UnitZ));
            view.camera.projection = Matrix(DirectX::XMMatrixOrthographicLH(8, 8, 0.1f, 100));
            view.camera.viewProjection = view.camera.view * view.camera.projection;
            view.camera.previousViewProjection = view.camera.viewProjection;
            view.camera.inverseView = view.camera.view.Invert();
            view.camera.inverseProjection = view.camera.projection.Invert();
            view.camera.inverseViewProjection = view.camera.viewProjection.Invert();
            view.camera.viewportSize = Vector2(256);
            view.camera.inverseViewportSize = Vector2(1.0f / 256);
            view.camera.farClip = 100;
            view.frustum = CameraUtils::buildWorldFrustum(view.camera.projection, view.camera.inverseView);
            return view;
        }

        bool draw(DX12Renderer& renderer, Scene* scene)
        {
            CameraRenderSubmissionQueue::instance().submit(overview());
            if (scene)
                scene->submitRenderData();
            const auto frame = FrameProfiler::instance().beginFrame();
            if (!frame)
                return false;
            const bool rendered = renderer.render();
            return FrameProfiler::instance().endFrame(frame, 1) && rendered;
        }

        bool erosionFrames(Checks& checks, DX12Renderer& renderer, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "Erosion frames"), reference(ObjectGUID::generate(), "Sync erosion frames");
            Scene empty(ObjectGUID::generate(), "No lights");
            LightingFrame::instance().capture(nullptr, empty);
            auto* terrain = scene.createGameObject("Async erosion")->addComponent<TerrainComponent>();
            auto* oracle = reference.createGameObject("Sync erosion")->addComponent<TerrainComponent>();
            TerrainNoiseSettings noise;
            noise.width = 65;
            noise.height = 41;
            noise.seed = 987;
            TerrainMaterialSettings material;
            material.enabled = true;
            TerrainErosionSettings erosion;
            erosion.seed = 723;
            if (!checks.expect(terrain->generateNoise(noise, { 4, 4, 2, 0, 3, 5 })
                && terrain->setMaterialSettings(material) && terrain->setChunkSettings({ 17 })
                && terrain->setLodSettings({ true, 4, 3 }) && terrain->copyConfigurationTo(*oracle)
                && oracle->applyErosion(erosion), "async erosion GPU synchronous oracle"))
                return false;
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(draw(renderer, &scene), "warm original erosion surface"))
                    return false;
            DX12FrameCapture before, pending, after, expected;
            if (!checks.expect(renderer.captureFrame(before) && before.save(directory, L"terrain-erosion-before")
                && terrain->requestErosion(erosion), "capture original and queue erosion"))
                return false;
            for (int frame = 0; frame < 4; ++frame)
                if (!checks.expect(draw(renderer, &scene), "render original surface during erosion"))
                    return false;
            if (!awaitReady(checks, *terrain)
                || !checks.expect(renderer.captureFrame(pending), "capture prepared but unapplied erosion"))
                return false;
            const auto equalPixels = [](const DX12FrameCapture& a, const DX12FrameCapture& b)
            {
                const auto* x = a.display.GetImage(0, 0, 0), *y = b.display.GetImage(0, 0, 0);
                return x && y && x->slicePitch == y->slicePitch && std::memcmp(x->pixels, y->pixels, x->slicePitch) == 0;
            };
            if (!checks.expect(equalPixels(before, pending), "erosion never publishes partial GPU pixels")
                || !awaitApplied(checks, *terrain) || !equivalent(checks, *terrain, *oracle))
                return false;
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(draw(renderer, &scene), "render eroded Deferred/LOD/material surface"))
                    return false;
            if (!checks.expect(renderer.captureFrame(after) && after.save(directory, L"terrain-erosion-after")
                && !equalPixels(before, after), "erosion creates visibly changed rendered terrain"))
                return false;
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(draw(renderer, &reference), "render synchronous erosion GPU oracle"))
                    return false;
            if (!checks.expect(renderer.captureFrame(expected) && equalPixels(after, expected),
                "Deferred asynchronous erosion pixels exactly match synchronous erosion"))
                return false;
            renderer.setDeferredRenderingEnabled(false);
            renderer.setOcclusionCullingEnabled(true);
            for (int frame = 0; frame < 4; ++frame)
                if (!checks.expect(draw(renderer, &scene), "Forward/Hi-Z renders eroded chunks"))
                    return false;
            if (!checks.expect(renderer.captureFrame(after), "capture Forward erosion surface"))
                return false;
            for (int frame = 0; frame < 4; ++frame)
                if (!checks.expect(draw(renderer, &reference), "Forward/Hi-Z synchronous erosion oracle"))
                    return false;
            if (!checks.expect(renderer.captureFrame(expected) && equalPixels(after, expected),
                "Forward asynchronous erosion pixels exactly match synchronous erosion"))
                return false;
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            scene.clear();
            reference.clear();
            for (int frame = 0; frame < 6; ++frame)
                if (!checks.expect(draw(renderer, nullptr), "retire eroded GPU resources"))
                    return false;
            return checks.expect(renderer.validateDebugMessages(), "eroded GPU resources produce no D3D12 diagnostics");
        }

        bool frames(Checks& checks, DX12Renderer& renderer, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "Live background terrain");
            Scene empty(ObjectGUID::generate(), "No lights");
            LightingFrame::instance().capture(nullptr, empty);
            auto* object = scene.createGameObject("Terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            Heightmap flat;
            TerrainMaterialSettings material;
            material.enabled = true;
            if (!checks.expect(flat.createFlat(65, 65, 0.3f) && terrain->setTerrain(flat, { 4, 4, 2, 0, 3, 5 })
                && terrain->setMaterialSettings(material) && terrain->setLodSettings({ true, 4, 1 }), "live-frame fixture"))
                return false;
            scene.update(0);
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(draw(renderer, &scene), "warm previous terrain render"))
                    return false;
            DX12FrameCapture previous, capture;
            if (!checks.expect(renderer.captureFrame(previous), "capture previous terrain"))
                return false;
            TerrainNoiseSettings maximum;
            maximum.width = maximum.height = 1025;
            maximum.octaves = 12;
            maximum.seed = 723;
            const auto oldModel = terrain->getModel();
            const auto oldCollision = collider->getPhysicsAsset();
            const auto started = std::chrono::steady_clock::now();
            if (!checks.expect(terrain->requestNoise(maximum, terrain->getSettings()), "queue maximum noise/mesh/layers/chunks/PhysX/LOD"))
                return false;
            const double requestMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            double maximumFrameMs = 0;
            std::size_t concurrentFrames = 0;
            for (int frame = 0; frame < 8; ++frame)
            {
                if (terrain->getGenerationStatus().stage != TerrainGenerationStage::Ready)
                    ++concurrentFrames;
                const auto start = std::chrono::steady_clock::now();
                if (!checks.expect(draw(renderer, &scene) && terrain->getModel() == oldModel
                    && collider->getPhysicsAsset() == oldCollision, "real frames continue drawing old terrain during CPU generation"))
                    return false;
                maximumFrameMs = std::max(maximumFrameMs, std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count());
                JobCounter counter;
                JobSystem::instance().schedule([] {}, &counter);
                JobSystem::instance().wait(counter);
                const auto hit = PhysicsSystem::instance().getWorld()->raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20);
                if (!checks.expect(hit && std::abs(hit->point.y - 0.6f) < 0.001f, "physics remains queryable while independent cooking runs"))
                    return false;
            }
            if (!checks.expect(concurrentFrames >= 3 && requestMs < 100 && maximumFrameMs < 150,
                "maximum generation is nonblocking: request below 100ms and concurrent frames below 150ms"))
                return false;
            if (!checks.expect(renderer.captureFrame(capture), "capture old surface during generation"))
                return false;
            const auto* a = previous.display.GetImage(0, 0, 0), *b = capture.display.GetImage(0, 0, 0);
            if (!checks.expect(a && b && a->slicePitch == b->slicePitch && std::memcmp(a->pixels, b->pixels, a->slicePitch) == 0,
                "no partial generation output appears in any GPU pixel"))
                return false;
            const auto cookingDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (terrain->getGenerationStatus().stage < TerrainGenerationStage::Collision
                && std::chrono::steady_clock::now() < cookingDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Collision,
                "observe real background PhysX cooking stage"))
                return false;
            auto* world = PhysicsSystem::instance().getWorld();
            world->simulate(1.0f / 60);
            world->fetchResults();
            const auto duringCooking = world->raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20);
            if (!checks.expect(duringCooking && std::abs(duringCooking->point.y - 0.6f) < 0.001f
                && draw(renderer, &scene), "existing PhysX simulation, query and GPU draw progress during worker cooking")
                || !awaitReady(checks, *terrain))
                return false;
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            ModelRenderSubmissionQueue::instance().consume(submissions);
            if (!awaitApplied(checks, *terrain))
                return false;
            const auto status = terrain->getGenerationStatus();
            if (!checks.expect(terrain->getHeightmap().width() == 1025 && terrain->getChunks().size() == 256
                && collider->getChunkAssets().size() == 256 && submissions.size() == 1
                && submissions[0].modelSnapshot->meshes.size() == 1,
                "maximum result replaces all CPU/collision state while old queued snapshot stays intact"))
                return false;
            ModelRenderSubmissionQueue::instance().submit(submissions[0]);
            if (!checks.expect(draw(renderer, nullptr) && renderer.captureFrame(capture), "draw old queued terrain after async replacement"))
                return false;
            b = capture.display.GetImage(0, 0, 0);
            if (!checks.expect(b && a->slicePitch == b->slicePitch && std::memcmp(a->pixels, b->pixels, a->slicePitch) == 0,
                "async replacement cannot retarget already queued GPU geometry"))
                return false;
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(draw(renderer, &scene), "render maximum async terrain after application"))
                    return false;
            if (!checks.expect(renderer.captureFrame(capture) && capture.save(directory, L"terrain-background")
                && renderer.getStatistics().terrainVisibleChunks == 256
                && renderer.getStatistics().terrainTriangles < renderer.getStatistics().terrainFullTriangles,
                "actual maximum async GPU draw keeps chunk LOD/material integration"))
                return false;
            std::ofstream metrics(directory / L"background-timing.txt");
            metrics << "Maximum 1025x1025 12-octave CPU generation with 256 PhysX/LOD chunks\n"
                << "request_ms=" << requestMs << "\nconcurrent_frames=" << concurrentFrames
                << "\nmax_concurrent_frame_ms=" << maximumFrameMs << "\nbuild_ms=" << status.buildMilliseconds
                << "\napply_ms=" << status.applyMilliseconds << "\nworker_thread=" << status.workerThread
                << "\napply_thread=" << status.applyThread << '\n';
            metrics.close();
            if (!checks.expect(static_cast<bool>(metrics), "write measured background timings"))
                return false;
            LOG_INFO("[TerrainBackgroundValidation] Request={}ms, {} concurrent frames (max={}ms), CPU={}ms, apply={}ms.",
                requestMs, concurrentFrames, maximumFrameMs, status.buildMilliseconds, status.applyMilliseconds);
            renderer.setDeferredRenderingEnabled(false);
            renderer.setOcclusionCullingEnabled(true);
            for (int frame = 0; frame < 4; ++frame)
                if (!checks.expect(draw(renderer, &scene), "Forward/Hi-Z renders the asynchronously prepared maximum grid"))
                    return false;
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            if (!checks.expect(terrain->requestFlat(65, 65, 0.3f, terrain->getSettings()), "queue async first-upload lifetime fixture")
                || !awaitApplied(checks, *terrain))
                return false;
            submissions.clear();
            scene.submitRenderData();
            const auto retired = terrain->getModel();
            scene.clear();
            if (!checks.expect(ModelManager::instance().get(retired) == nullptr
                && draw(renderer, nullptr) && renderer.captureFrame(capture), "unuploaded async result draws after owner destruction"))
                return false;
            b = capture.display.GetImage(0, 0, 0);
            if (!checks.expect(b && a->slicePitch == b->slicePitch && std::memcmp(a->pixels, b->pixels, a->slicePitch) == 0,
                "destroyed async owner leaves an exact queued CPU/material/LOD snapshot"))
                return false;
            for (int frame = 0; frame < 6; ++frame)
                if (!checks.expect(draw(renderer, nullptr), "retire async GPU snapshots behind fences"))
                    return false;
            return checks.expect(renderer.validateDebugMessages(), "background terrain rendering has no D3D12 diagnostics");
        }
    }

    bool validateTerrainBackground(DX12Renderer& renderer, const std::filesystem::path& directory, std::size_t& passed)
    {
        passed = 0;
        Checks checks;
        if (!parity(checks, directory) || !lifecycle(checks, directory) || !physicsRollback(checks)
            || !erosionFrames(checks, renderer, directory) || !frames(checks, renderer, directory))
            return false;
        TerrainGenerationQueue::instance().shutdown();
        passed = checks.count;
        LOG_INFO("[TerrainBackgroundValidation] PASS: {} checks.", passed);
        return true;
    }
}
