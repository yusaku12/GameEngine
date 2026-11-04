#include "Pch.h"
#include "Validation\Terrain\TerrainLodValidation.h"
#include "Terrain\Geometry\TerrainLod.h"
#include "Components\Terrain\TerrainComponent.h"
#include "Components\Rendering\LightComponent.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Assets\Model\ModelManager.h"
#include "Components\Physics\Collider\TerrainColliderComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\Camera\CameraUtils.h"
#include "Graphics\DirectX12\Rendering\Renderer.h"
#include "Graphics\DirectX12\Diagnostics\FrameCapture.h"
#include "Graphics\Lighting\LightingFrame.h"
#include "Generated\FlatBuffers\TerrainComponent_generated.h"
#include "Terrain\Serialization\TerrainMaterialSerialization.h"
#include <map>
#include <cmath>
#include <limits>

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
                    LOG_ERROR("[TerrainLodValidation] FAILED: {}.", label);
                else
                    ++count;
                return value;
            }
        };

        bool topology(Checks& checks)
        {
            for (const auto size : { std::pair{ 16u, 16u }, { 17u, 31u }, { 64u, 64u }, { 40u, 35u },
                { 1u, 31u }, { 2u, 32u }, { 3u, 3u }, { 1024u, 2u }, { 2u, 1024u } })
            {
                Heightmap heights;
                ModelResource model;
                std::vector<TerrainChunk> chunks;
                if (!checks.expect(heights.createFlat(size.first + 1, size.second + 1, 0.5f)
                    && buildTerrainModel(heights, {}, model)
                    && partitionTerrainModel(model, heights.width(), heights.height(), { 1024 }, chunks),
                    "LOD topology fixture including partial/narrow/non-power-of-two grids"))
                    return false;
                const auto originalIndices = model.meshes[0].indices;
                const auto originalVertices = model.meshes[0].vertices;
                TerrainLodSnapshot snapshot;
                if (!checks.expect(buildTerrainLods(model, chunks, { true, 4, 10 }, snapshot)
                    && snapshot.chunks.size() == 1, "build index-only levels"))
                    return false;
                const auto& mesh = model.meshes[0];
                const auto& lod = snapshot.chunks[0];
                if (!checks.expect(std::equal(originalIndices.begin(), originalIndices.end(), mesh.indices.begin())
                    && mesh.subMeshes[0].indexCount == originalIndices.size()
                    && mesh.vertices.size() == originalVertices.size(), "LOD zero and full-resolution vertex buffer unchanged"))
                    return false;
                for (std::size_t index = 0; index < originalVertices.size(); ++index)
                    if (!checks.expect(mesh.vertices[index].position == originalVertices[index].position
                        && mesh.vertices[index].normal == originalVertices[index].normal
                        && mesh.vertices[index].texCoord == originalVertices[index].texCoord
                        && mesh.vertices[index].color == originalVertices[index].color,
                        "LOD never changes vertex geometry or material attributes"))
                        return false;
                for (std::uint32_t level = 0; level < lod.levelCount; ++level)
                {
                    const auto range = lod.levels[level];
                    if (!checks.expect(range.start + range.count <= mesh.indices.size() && range.count % 3 == 0
                        && (level == 0 || range.count < lod.levels[level - 1].count),
                        "LOD range is in bounds and strictly reduces triangle count"))
                        return false;
                    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> edges;
                    std::uint64_t area = 0;
                    const auto stride = size.first + 1;
                    for (std::uint32_t index = range.start; index < range.start + range.count; index += 3)
                    {
                        const auto a = mesh.indices[index], b = mesh.indices[index + 1], c = mesh.indices[index + 2];
                        if (!checks.expect(a < mesh.vertices.size() && b < mesh.vertices.size() && c < mesh.vertices.size(),
                            "every LOD triangle references a full-resolution vertex"))
                            return false;
                        const auto ax = static_cast<std::int64_t>(a % stride), az = static_cast<std::int64_t>(a / stride);
                        const auto bx = static_cast<std::int64_t>(b % stride), bz = static_cast<std::int64_t>(b / stride);
                        const auto cx = static_cast<std::int64_t>(c % stride), cz = static_cast<std::int64_t>(c / stride);
                        const auto signedArea = (bz - az) * (cx - ax) - (bx - ax) * (cz - az);
                        if (!checks.expect(signedArea > 0, "LOD has no degenerate/reversed triangles"))
                            return false;
                        area += static_cast<std::uint64_t>(signedArea);
                        for (const auto edge : { std::pair{ a, b }, { b, c }, { c, a } })
                            ++edges[std::minmax(edge.first, edge.second)];
                    }
                    std::size_t borderEdges = 0;
                    for (const auto& [edge, count] : edges)
                    {
                        const auto ax = edge.first % stride, az = edge.first / stride;
                        const auto bx = edge.second % stride, bz = edge.second / stride;
                        const bool border = (ax == bx && (ax == 0 || ax == size.first))
                            || (az == bz && (az == 0 || az == size.second));
                        if (!checks.expect(count == (border ? 1u : 2u), "LOD manifold has only the true outer boundary"))
                            return false;
                        if (border)
                        {
                            ++borderEdges;
                            if (!checks.expect((ax == bx ? bz - az : bx - ax) == 1,
                                "every full-resolution unit border edge survives at every level"))
                                return false;
                        }
                    }
                    if (!checks.expect(area == static_cast<std::uint64_t>(size.first) * size.second * 2
                        && borderEdges == 2 * (size.first + size.second), "LOD covers chunk exactly without holes or overlaps"))
                        return false;
                }
                if (size.first == 64 && size.second == 64
                    && !checks.expect(lod.levels[lod.levelCount - 1].count * 5 < lod.levels[0].count,
                        "64-cell coarse LOD reduces triangles by more than 80 percent including seam ring"))
                    return false;
                if (size.first == 64 && size.second == 64)
                {
                    const auto previousCount = mesh.indices.size();
                    const auto previousLevelCount = lod.levelCount;
                    if (!checks.expect(!buildTerrainLods(model, chunks, { true, 4, 10 }, snapshot)
                        && mesh.indices.size() == previousCount && snapshot.chunks[0].levelCount == previousLevelCount,
                        "attempting to append duplicate LOD tails is rejected without changing outputs"))
                        return false;
                }
            }
            const AABB bounds(Vector3::Zero, Vector3(1));
            const TerrainLodSettings settings{ true, 4, 10 };
            for (std::uint32_t level = 1; level <= 4; ++level)
            {
                const float threshold = 10.0f * static_cast<float>(1u << (level - 1));
                if (!checks.expect(selectTerrainLod(settings, bounds, Vector3(threshold + 1, 0, 0), 5) == level
                    && selectTerrainLod(settings, bounds, Vector3(std::nextafter(threshold + 1, 0.0f), 0, 0), 5) == level - 1,
                    "distance level transitions occur at exact doubled thresholds"))
                    return false;
            }
            return checks.expect(selectTerrainLod(settings, bounds, Vector3::Zero, 5) == 0
                && selectTerrainLod({ false, 4, 10 }, bounds, Vector3(10000), 5) == 0
                && selectTerrainLod(settings, bounds, Vector3(10000), 2) == 1,
                "inside bounds, disabled LOD and narrow chunks select valid levels");
        }

        bool persistence(Checks& checks, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "Terrain LOD source");
            auto* object = scene.createGameObject("LOD terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            TerrainNoiseSettings noise;
            noise.width = 65;
            noise.height = 41;
            const TerrainLodSettings settings{ true, 3, 17 };
            if (!checks.expect(!terrain->getLodSettings().enabled && terrain->setChunkSettings({ 16 })
                && terrain->generateNoise(noise, { 64, 40, 4, 1, 2, 3 }) && terrain->setLodSettings(settings),
                "LOD defaults off and enables explicitly"))
                return false;
            const auto model = terrain->getModel();
            const auto snapshot = terrain->getLodSnapshot();
            for (const auto invalid : { TerrainLodSettings{ true, 0, 10 }, { true, 5, 10 },
                { true, 2, 0 }, { true, 2, INFINITY }, { false, 2, NAN }, { true, 2, 1000001 } })
                if (!checks.expect(!terrain->setLodSettings(invalid) && terrain->getModel() == model
                    && terrain->getLodSnapshot() == snapshot && terrain->getLodSettings() == settings,
                    "invalid applied LOD settings preserve resources/snapshot"))
                    return false;
            std::vector<std::uint8_t> payload;
            TerrainComponent restored;
            if (!checks.expect(terrain->serializePayload(payload) && restored.deserializePayload(5, payload)
                && restored.getLodSettings() == settings && restored.getChunks().size() == terrain->getChunks().size(),
                "v5 roundtrip retains LOD settings and chunk layout"))
                return false;
            for (const auto version : { 1u, 2u, 3u, 4u })
                if (!checks.expect(restored.deserializePayload(version, payload) && !restored.getLodSettings().enabled,
                    "legacy versions retain full-resolution rendering"))
                    return false;
            for (int invalid = 0; invalid < 4; ++invalid)
            {
                flatbuffers::FlatBufferBuilder builder;
                flatbuffers::Offset<Serialization::TerrainMaterialData> material;
                if (!Serialization::writeTerrainMaterial(builder, terrain->getMaterialSettings(), material))
                    return false;
                const auto recipe = Serialization::CreateTerrainNoiseSettings(builder);
                const auto lod = invalid == 0 ? flatbuffers::Offset<Serialization::TerrainLodSettings>{}
                    : Serialization::CreateTerrainLodSettings(builder, true, invalid == 1 ? 9 : 2,
                        invalid == 2 ? NAN : invalid == 3 ? 0.0f : 10.0f);
                const auto samples = terrain->getHeightmap().samples();
                const auto root = Serialization::CreateTerrainComponentPayload(builder, noise.width, noise.height,
                    builder.CreateVector(samples.data(), samples.size()), 64, 40, 4, 1, 2, 3, true, recipe, material, 16, lod);
                Serialization::FinishTerrainComponentPayloadBuffer(builder, root);
                if (!checks.expect(!terrain->deserializePayload(5, { builder.GetBufferPointer(), builder.GetSize() })
                    && terrain->getLodSnapshot() == snapshot, "missing or malformed v5 LOD settings reject transactionally"))
                    return false;
            }
            Scene loaded(ObjectGUID::generate(), "Loaded LOD");
            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"lod.scene", scene)
                && Serialization::SceneSerializer{}.load(directory / L"lod.scene", loaded)
                && loaded.find(object->getGUID())->getComponent<TerrainComponent>()->getLodSettings() == settings,
                "LOD Scene roundtrip"))
                return false;
            Prefab prefab, loadedPrefab;
            Scene instances(ObjectGUID::generate(), "LOD prefab");
            if (!checks.expect(prefab.capture(*object)
                && Serialization::PrefabSerializer{}.save(directory / L"lod.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"lod.prefab", loadedPrefab), "LOD Prefab roundtrip"))
                return false;
            auto* instance = loadedPrefab.instantiate(instances);
            if (!checks.expect(instance && instance->getComponent<TerrainComponent>()->getLodSettings() == settings,
                "Prefab restores applied LOD"))
                return false;
            Scene clone(ObjectGUID::generate(), "LOD Play");
            const SceneClonePair pair{ scene, clone };
            if (!checks.expect(cloneScenes(std::span(&pair, 1))
                && clone.find(object->getGUID())->getComponent<TerrainComponent>()->getLodSettings() == settings
                && clone.find(object->getGUID())->getComponent<TerrainComponent>()->getLodSnapshot() != snapshot,
                "Play clone owns independent immutable LOD geometry"))
                return false;
            auto* collider = object->addComponent<TerrainColliderComponent>();
            scene.update(0);
            auto* world = PhysicsSystem::instance().getWorld();
            if (!checks.expect(world && collider->getChunkAssets().size() == terrain->getChunks().size(),
                "late-added collider cooks full-resolution geometry despite appended LOD indices"))
                return false;
            const auto cpuModel = ModelManager::instance().get(model);
            for (std::size_t index = 0; index < cpuModel->meshes.size(); ++index)
            {
                const auto& mesh = cpuModel->meshes[index];
                std::vector<Vector3> vertices;
                for (const auto& vertex : mesh.vertices)
                    vertices.push_back(vertex.position);
                const auto expected = PhysicsMeshAsset::cookTriangle(vertices,
                    std::span(mesh.indices).first(mesh.subMeshes[0].indexCount));
                const auto actual = collider->getChunkAssets()[index]->getCookedData();
                if (!checks.expect(expected && std::ranges::equal(expected->getCookedData(), actual),
                    "cooked PhysX bytes equal full-resolution-only oracle, no duplicate LOD triangles"))
                    return false;
            }
            for (int z = -15; z <= 15; z += 5)
                for (int x = -25; x <= 25; x += 5)
                {
                    const Vector3 position(x + 0.17f, 20, z + 0.31f);
                    const auto surface = terrain->sampleWorldSurface(position);
                    const auto hit = world->raycast(position, -Vector3::UnitY, 40);
                    if (!checks.expect(surface && hit && hit->collider == collider
                        && std::abs(surface->position.y - hit->point.y) < 0.001f,
                        "LOD-enabled physics height remains identical to authoritative surface"))
                        return false;
                }
            ModelRenderSubmissionQueue::instance().clear();
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            ModelRenderSubmissionQueue::instance().consume(submissions);
            if (!checks.expect(submissions.size() == 1 && submissions[0].terrainLod == snapshot
                && terrain->setLodSettings({ false, 4, 10 }) && submissions[0].terrainLod->settings.enabled,
                "queued LOD snapshot is unaffected by subsequent authoring"))
                return false;
            scene.clear();
            return checks.expect(submissions[0].terrainLod->chunks.size() == snapshot->chunks.size()
                && submissions[0].modelSnapshot->meshes[0].indices.size() > snapshot->chunks[0].levels[0].count,
                "queued LOD ranges and geometry survive destruction together");
        }

        RenderView viewAt(const Vector3& position, const float width)
        {
            RenderView view;
            view.camera.position = position;
            view.camera.forward = -Vector3::UnitY;
            view.camera.view = Matrix(DirectX::XMMatrixLookAtLH(position, position - Vector3::UnitY, Vector3::UnitZ));
            view.camera.projection = Matrix(DirectX::XMMatrixOrthographicLH(width, width, 0.1f, 200));
            view.camera.viewProjection = view.camera.view * view.camera.projection;
            view.camera.previousViewProjection = view.camera.viewProjection;
            view.camera.inverseView = view.camera.view.Invert();
            view.camera.inverseProjection = view.camera.projection.Invert();
            view.camera.inverseViewProjection = view.camera.viewProjection.Invert();
            view.camera.viewportSize = Vector2(256);
            view.camera.inverseViewportSize = Vector2(1.0f / 256);
            view.camera.farClip = 200;
            view.backgroundColor = Color(0.01f, 0.02f, 0.04f, 1);
            view.frustum = CameraUtils::buildWorldFrustum(view.camera.projection, view.camera.inverseView);
            return view;
        }

        bool frame(DX12Renderer& renderer, Scene* scene, const RenderView& view)
        {
            CameraRenderSubmissionQueue::instance().submit(view);
            if (scene)
                scene->submitRenderData();
            const auto id = FrameProfiler::instance().beginFrame();
            if (!id)
                return false;
            const bool rendered = renderer.render();
            return FrameProfiler::instance().endFrame(id, 1) && rendered;
        }

        bool samePixels(Checks& checks, const DX12FrameCapture& a, const DX12FrameCapture& b)
        {
            const auto* left = a.display.GetImage(0, 0, 0);
            const auto* right = b.display.GetImage(0, 0, 0);
            if (!checks.expect(left && right && left->width == right->width && left->height == right->height
                && left->format == DXGI_FORMAT_R8G8B8A8_UNORM && right->format == left->format, "LOD capture format"))
                return false;
            std::size_t differences = 0;
            for (std::size_t z = 0; z < left->height; ++z)
                for (std::size_t x = 0; x < left->width * 4; ++x)
                    if (std::abs(static_cast<int>(left->pixels[z * left->rowPitch + x])
                        - static_cast<int>(right->pixels[z * right->rowPitch + x])) > 2)
                        ++differences;
            if (differences)
                LOG_ERROR("[TerrainLodValidation] Flat LOD changed {} display channels.", differences);
            return checks.expect(differences == 0, "flat mixed-LOD mesh matches full-resolution surface at every pixel");
        }

        bool rendering(Checks& checks, DX12Renderer& renderer, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "LOD GPU");
            Scene empty(ObjectGUID::generate(), "Empty persistent lights");
            LightingFrame::instance().capture(nullptr, empty);
            auto* object = scene.createGameObject("LOD");
            auto* terrain = object->addComponent<TerrainComponent>();
            Heightmap flat;
            if (!checks.expect(flat.createFlat(65, 65, 0.5f) && terrain->setChunkSettings({ 16 })
                && terrain->setTerrain(flat, { 64, 64, 0, 0, 2, 3 }), "mixed-LOD flat GPU fixture"))
                return false;
            const auto overview = viewAt(Vector3(-24, 5, 0), 128);
            DX12FrameCapture reference, capture;
            for (const bool deferred : { true, false })
            {
                renderer.setDeferredRenderingEnabled(deferred);
                renderer.setOcclusionCullingEnabled(!deferred);
                if (!checks.expect(terrain->setLodSettings({ false, 4, 12 }), "full-resolution GPU reference"))
                    return false;
                for (int repeat = 0; repeat < 3; ++repeat)
                    if (!checks.expect(frame(renderer, &scene, overview), "draw full-resolution reference"))
                        return false;
                if (!checks.expect(renderer.captureFrame(reference) && terrain->setLodSettings({ true, 4, 12 }),
                    "enable distance LOD for GPU"))
                    return false;
                for (int repeat = 0; repeat < 4; ++repeat)
                    if (!checks.expect(frame(renderer, &scene, overview), "draw mixed LODs"))
                        return false;
                const auto stats = renderer.getStatistics();
                const auto activeLevels = std::ranges::count_if(stats.terrainLodChunks, [](auto count) { return count != 0; });
                if (!checks.expect(stats.terrainVisibleChunks == 16 && activeLevels >= 2
                    && stats.terrainTriangles * 2 < stats.terrainFullTriangles,
                    "GPU draw selection uses mixed levels and reduces submitted triangles by more than half")
                    || !checks.expect(renderer.captureFrame(capture) && renderer.validateDebugMessages(), "capture actual LOD draws")
                    || !samePixels(checks, reference, capture))
                    return false;
                auto close = viewAt(Vector3(-24, 1, 24), 6);
                if (!checks.expect(frame(renderer, &scene, close), "next camera selects independent close LOD"))
                    return false;
                const auto closeStats = renderer.getStatistics();
                if (!checks.expect(closeStats.terrainVisibleChunks == 1 && closeStats.terrainCulledChunks == 15
                    && closeStats.terrainLodChunks[0] == 1 && closeStats.terrainTriangles == closeStats.terrainFullTriangles,
                    "per-camera chunk culling retains only nearby full-resolution chunk"))
                    return false;
                auto distant = viewAt(Vector3(0, 100, 0), 128);
                if (!checks.expect(frame(renderer, &scene, distant), "next camera selects independent far LOD")
                    || !checks.expect(renderer.getStatistics().terrainVisibleChunks == 16
                        && renderer.getStatistics().terrainLodChunks[0] == 0, "far camera does not inherit close camera LOD"))
                    return false;
                auto outside = viewAt(Vector3(200, 20, 200), 6);
                if (!checks.expect(frame(renderer, &scene, outside)
                    && renderer.getStatistics().terrainVisibleChunks == 0 && renderer.getStatistics().terrainCulledChunks == 16,
                    "whole-terrain outside view culls every chunk"))
                    return false;
                auto masked = overview;
                masked.cullingMask = 0;
                if (!checks.expect(frame(renderer, &scene, masked)
                    && renderer.getStatistics().terrainVisibleChunks == 0 && renderer.getStatistics().terrainCulledChunks == 16,
                    "camera layer mask excludes terrain chunks"))
                    return false;
            }
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            auto oblique = viewAt(Vector3(50, 40, -50), 90);
            oblique.camera.forward = -oblique.camera.position;
            oblique.camera.forward.Normalize();
            oblique.camera.view = Matrix(DirectX::XMMatrixLookAtLH(oblique.camera.position, Vector3::Zero, Vector3::UnitY));
            oblique.camera.viewProjection = oblique.camera.view * oblique.camera.projection;
            oblique.camera.inverseView = oblique.camera.view.Invert();
            oblique.camera.inverseViewProjection = oblique.camera.viewProjection.Invert();
            oblique.frustum = CameraUtils::buildWorldFrustum(oblique.camera.projection, oblique.camera.inverseView);
            if (!checks.expect(terrain->setLodSettings({ false, 4, 12 }) && frame(renderer, &scene, oblique)
                && renderer.captureFrame(reference) && terrain->setLodSettings({ true, 4, 12 })
                && frame(renderer, &scene, oblique) && renderer.captureFrame(capture),
                "oblique mixed-LOD boundary capture") || !samePixels(checks, reference, capture))
                return false;
            const auto* image = capture.display.GetImage(0, 0, 0);
            const auto* background = image->pixels;
            std::size_t covered = 0;
            for (std::size_t y = 0; y < image->height; ++y)
                for (std::size_t x = 0; x < image->width; ++x)
                    if (std::memcmp(background, image->pixels + y * image->rowPitch + x * 4, 3) != 0)
                        ++covered;
            if (!checks.expect(covered > 10000, "LOD pixel oracle compares an actual surface, not empty captures"))
                return false;
            auto* parent = scene.createGameObject("LOD transformed parent");
            parent->getTransform()->setPosition(Vector3(18, 3, -7));
            parent->getTransform()->setScale(Vector3(1.7f, 1.2f, 0.6f));
            parent->getTransform()->setEulerAngles(0, 0.37f, 0);
            object->getTransform()->setPosition(Vector3(-5, 1, 7));
            object->getTransform()->setEulerAngles(0, -0.41f, 0);
            if (!checks.expect(object->setParent(parent, false), "transformed terrain parent fixture"))
                return false;
            const auto transformedView = viewAt(Vector3(10, 30, -12), 256);
            const auto transformedModel = ModelManager::instance().get(terrain->getModel());
            const auto transformedLod = terrain->getLodSnapshot();
            std::array<std::uint32_t, 5> expectedLevels{};
            for (std::size_t index = 0; index < transformedModel->meshes.size(); ++index)
            {
                Vector3 minimum(FLT_MAX), maximum(-FLT_MAX);
                for (const auto& vertex : transformedModel->meshes[index].vertices)
                {
                    const auto point = Vector3::Transform(vertex.position, object->getWorldMatrix());
                    minimum = Vector3::Min(minimum, point);
                    maximum = Vector3::Max(maximum, point);
                }
                auto closest = transformedView.camera.position;
                closest.Clamp(minimum, maximum);
                const double distance = Vector3::Distance(closest, transformedView.camera.position);
                std::uint32_t level = 0;
                double threshold = 12;
                while (level + 1 < transformedLod->chunks[index].levelCount && distance >= threshold)
                {
                    ++level;
                    threshold *= 2;
                }
                ++expectedLevels[level];
            }
            if (!checks.expect(frame(renderer, &scene, transformedView)
                && renderer.getStatistics().terrainVisibleChunks == 16
                && renderer.getStatistics().terrainLodChunks == expectedLevels,
                "GPU LOD distances use full parent rotation/translation/nonuniform scale in world units"))
                return false;
            if (!checks.expect(object->setParent(nullptr, false), "restore terrain after transformed LOD test"))
                return false;
            object->getTransform()->setPosition(Vector3::Zero);
            object->getTransform()->setEulerAngles(0, 0, 0);
            TerrainNoiseSettings noise;
            noise.width = 65;
            noise.height = 41;
            noise.seed = 713;
            TerrainMaterialSettings layers;
            layers.enabled = true;
            if (!checks.expect(terrain->generateNoise(noise, { 64, 40, 8, 0, 2, 3 })
                && terrain->setMaterialSettings(layers) && frame(renderer, &scene, oblique)
                && renderer.getStatistics().terrainTriangles < renderer.getStatistics().terrainFullTriangles
                && renderer.captureFrame(capture) && capture.save(directory, L"terrain-lod-layers")
                && renderer.validateDebugMessages(), "render partial nonflat LOD chunks with all four terrain layers")
                || !checks.expect(terrain->setTerrain(flat, { 64, 64, 0, 0, 2, 3 })
                    && terrain->setMaterialSettings({}), "restore flat LOD fixture"))
                return false;
            auto* lightObject = scene.createGameObject("Independent terrain shadow");
            auto* light = lightObject->addComponent<LightComponent>();
            LightSettings lighting;
            lighting.castsShadow = true;
            lighting.shadowDistance = 150;
            if (!checks.expect(light->setSettings(lighting), "terrain shadow fixture"))
                return false;
            LightingFrame::instance().capture(&scene, empty);
            const auto close = viewAt(Vector3(-24, 5, 24), 6);
            if (!checks.expect(frame(renderer, &scene, close), "camera-culling with independent terrain shadows"))
                return false;
            auto stats = renderer.getStatistics();
            if (!checks.expect(stats.terrainVisibleChunks == 1 && stats.terrainShadowChunks > stats.terrainVisibleChunks
                && stats.terrainShadowTriangles == static_cast<std::uint64_t>(stats.terrainShadowChunks) * 512,
                "off-camera chunks cast full-resolution shadows independently of color LOD"))
                return false;
            const auto shadowOnly = viewAt(Vector3(40, 5, 24), 6);
            if (!checks.expect(frame(renderer, &scene, shadowOnly)
                && renderer.getStatistics().terrainVisibleChunks == 0 && renderer.getStatistics().terrainShadowChunks > 0,
                "whole-terrain camera rejection does not discard visible shadow casters"))
                return false;
            const auto distantShadow = viewAt(Vector3(500, 5, 500), 6);
            if (!checks.expect(frame(renderer, &scene, distantShadow)
                && renderer.getStatistics().terrainShadowChunks == 0, "shadow frustum rejects distant terrain chunks"))
                return false;
            lighting.shadowDistance = 10;
            if (!checks.expect(light->setSettings(lighting), "narrow independent shadow fixture"))
                return false;
            LightingFrame::instance().capture(&scene, empty);
            if (!checks.expect(frame(renderer, &scene, close)
                && renderer.getStatistics().terrainShadowChunks > 0 && renderer.getStatistics().terrainShadowChunks < 16,
                "shadow frustum culls individual chunks, not only complete terrains"))
                return false;
            LightingFrame::instance().capture(nullptr, empty);
            if (!checks.expect(frame(renderer, &scene, overview) && renderer.captureFrame(capture)
                && capture.save(directory, L"terrain-lod") && renderer.captureFrame(reference),
                "save actual mixed-LOD GPU capture and same-pipeline lifetime reference"))
                return false;
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            ModelRenderSubmissionQueue::instance().consume(submissions);
            if (!checks.expect(submissions.size() == 1, "malformed LOD snapshot fixture"))
                return false;
            for (int invalid = 0; invalid < 4; ++invalid)
            {
                auto bad = std::make_shared<TerrainLodSnapshot>(*terrain->getLodSnapshot());
                if (invalid == 3)
                    bad->chunks.pop_back();
                else
                    for (auto& chunk : bad->chunks)
                    {
                        if (invalid < 2)
                            chunk.levelCount = invalid == 0 ? 0u : 6u;
                        else
                            chunk.levels[1] = { UINT32_MAX, UINT32_MAX };
                    }
                submissions[0].terrainLod = std::move(bad);
                ModelRenderSubmissionQueue::instance().submit(submissions[0]);
                if (!checks.expect(frame(renderer, nullptr, overview) && renderer.getStatistics().terrainVisibleChunks == 0
                    && renderer.validateDebugMessages(), "malformed queued LOD metadata is logged and never drawn"))
                    return false;
            }
            submissions.clear();
            if (!checks.expect(terrain->setLodSettings({ true, 4, 12 }), "new unuploaded LOD model lifetime fixture"))
                return false;
            scene.submitRenderData();
            const auto retiredModel = terrain->getModel();
            scene.clear();
            if (!checks.expect(ModelManager::instance().get(retiredModel) == nullptr
                && frame(renderer, nullptr, overview) && renderer.captureFrame(capture)
                && renderer.getStatistics().terrainVisibleChunks == 16
                && renderer.getStatistics().terrainTriangles < renderer.getStatistics().terrainFullTriangles,
                "queued LOD index ranges survive before first use and component destruction")
                || !samePixels(checks, reference, capture))
                return false;
            for (int repeat = 0; repeat < 6; ++repeat)
                if (!checks.expect(frame(renderer, nullptr, overview), "retire LOD buffers after GPU fences"))
                    return false;
            return checks.expect(renderer.validateDebugMessages(), "LOD GPU teardown has no diagnostics");
        }
    }

    bool validateTerrainLod(DX12Renderer& renderer, const std::filesystem::path& directory, std::size_t& passed)
    {
        passed = 0;
        Checks checks;
        if (!topology(checks) || !persistence(checks, directory) || !rendering(checks, renderer, directory))
            return false;
        passed = checks.count;
        LOG_INFO("[TerrainLodValidation] PASS: {} checks.", passed);
        return true;
    }
}
