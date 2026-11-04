#include "Pch.h"
#include "Validation\Terrain\TerrainVegetationValidation.h"
#include "Terrain\Serialization\TerrainMaterialSerialization.h"
#include "Components\Terrain\TerrainComponent.h"
#include "Components\Rendering\LightComponent.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Threading\ThreadUtility.h"
#include "Assets\Model\ModelManager.h"
#include "Assets\Model\Serialization\ModelSerializer.h"
#include "Assets\Material\MaterialManager.h"
#include "Generated\FlatBuffers\TerrainComponent_generated.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\Camera\CameraUtils.h"
#include "Graphics\DirectX12\Rendering\Renderer.h"
#include "Graphics\DirectX12\Diagnostics\FrameCapture.h"
#include "Graphics\Lighting\LightingFrame.h"
#include "Components\Physics\Collider\TerrainColliderComponent.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\PhysicsSystem.h"
#include <cmath>
#include <cstring>
#include <fstream>

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
                    LOG_ERROR("[TerrainVegetationValidation] FAILED: {}.", label);
                else
                    ++count;
                return value;
            }
        };

        bool same(const TerrainVegetationPlacement& a, const TerrainVegetationPlacement& b)
        {
            for (std::size_t index = 0; index < a.size(); ++index)
            {
                if (static_cast<bool>(a[index]) != static_cast<bool>(b[index]))
                    return false;
                if (a[index] && (a[index]->size() != b[index]->size()
                    || !std::ranges::equal(*a[index], *b[index], [](const Matrix& x, const Matrix& y)
                    { return std::ranges::equal(std::span(&x._11, 16), std::span(&y._11, 16)); })))
                    return false;
            }
            return true;
        }

        TerrainVegetationSettings recipe(const std::filesystem::path& path)
        {
            TerrainVegetationSettings settings;
            settings.enabled = true;
            settings.layers[0].enabled = true;
            settings.layers[0].modelPath = path;
            settings.layers[0].seed = 987;
            settings.layers[0].density = 4;
            settings.layers[0].minimumSpacing = 0.6f;
            settings.layers[0].maxInstances = 64;
            settings.layers[0].maximumSlope = 90;
            return settings;
        }

        bool pure(Checks& checks)
        {
            Heightmap heights;
            TerrainMeshSettings mesh{ 40, 28, 4, -1, 1, 1 };
            const std::array<float, 4> ramp{ 0, 1, 0, 1 };
            auto settings = recipe(L"not-loaded-by-worker.model");
            auto& layer = settings.layers[0];
            layer.density = 0.5f;
            layer.maxInstances = 5000;
            layer.minimumSpacing = 1.25f;
            layer.minimumHeight = 0.2f;
            layer.maximumHeight = 0.8f;
            layer.minimumSlope = 5;
            layer.maximumSlope = 6;
            layer.alignToNormal = true;
            layer.heightOffset = 0.13f;
            TerrainVegetationPlacement first, repeat;
            if (!checks.expect(heights.assign(2, 2, ramp) && buildTerrainVegetation(heights, mesh, settings, first)
                && first[0] && first[0]->size() > 100 && buildTerrainVegetation(heights, mesh, settings, repeat)
                && same(first, repeat), "seeded candidates, filter and exact transform reproducibility"))
                return false;
            for (std::size_t index = 0; index < first[0]->size(); ++index)
            {
                const auto& transform = (*first[0])[index];
                Vector3 up(transform._21, transform._22, transform._23);
                const float scale = up.Length();
                up /= scale;
                const Vector3 root = transform.Translation() - up * layer.heightOffset;
                const auto surface = sampleTerrainSurface(heights, mesh, root.x, root.z);
                const Vector3 right(transform._11, transform._12, transform._13);
                const Vector3 forward(transform._31, transform._32, transform._33);
                if (!checks.expect(surface && std::abs(surface->position.y - root.y) < 2e-6f
                    && up.Dot(surface->normal) > 0.99999f && scale >= layer.minimumScale && scale <= layer.maximumScale
                    && std::abs(right.Length() - scale) < 2e-6f && std::abs(forward.Length() - scale) < 2e-6f
                    && std::abs(right.Dot(up)) < 2e-6f && transform.Determinant() > 0
                    && root.x >= -12.0001f && root.x <= 12.0001f,
                    "triangle-grounded roots, inclusive normalized height filter, positive scale and orthonormal slope alignment"))
                    return false;
                for (std::size_t other = 0; other < index; ++other)
                {
                    Vector3 otherUp((*first[0])[other]._21, (*first[0])[other]._22, (*first[0])[other]._23);
                    otherUp.Normalize();
                    const auto previous = (*first[0])[other].Translation() - otherUp * layer.heightOffset;
                    const double dx = static_cast<double>(root.x) - previous.x;
                    const double dz = static_cast<double>(root.z) - previous.z;
                    if (!checks.expect(dx * dx + dz * dz >= static_cast<double>(layer.minimumSpacing) * layer.minimumSpacing - 1e-5,
                        "every accepted root pair satisfies measured XZ minimum spacing"))
                        return false;
                }
            }
            const auto saved = first;
            auto extremes = settings;
            extremes.enabled = false;
            for (auto& item : extremes.layers)
            {
                item.density = 100;
                item.minimumSpacing = 10000;
                item.maxInstances = 25000;
                item.minimumHeight = item.maximumHeight = 1;
                item.minimumSlope = item.maximumSlope = 90;
                item.minimumScale = item.maximumScale = 1000;
                item.heightOffset = 1000;
            }
            if (!checks.expect(validateTerrainVegetation(extremes, mesh), "all numeric upper bounds are inclusive"))
                return false;
            for (auto& item : extremes.layers)
            {
                item.density = 0;
                item.minimumSpacing = 0.1f;
                item.maxInstances = 1;
                item.minimumHeight = item.maximumHeight = 0;
                item.minimumSlope = item.maximumSlope = 0;
                item.minimumScale = item.maximumScale = 0.001f;
                item.heightOffset = -1000;
            }
            if (!checks.expect(validateTerrainVegetation(extremes, mesh), "all numeric lower bounds are inclusive"))
                return false;
            const std::array invalidBounds{
                std::pair{ &TerrainVegetationLayer::density, 100.01f },
                std::pair{ &TerrainVegetationLayer::minimumSpacing, 0.099f },
                std::pair{ &TerrainVegetationLayer::minimumSpacing, 10000.1f },
                std::pair{ &TerrainVegetationLayer::minimumHeight, -0.001f },
                std::pair{ &TerrainVegetationLayer::maximumHeight, 1.001f },
                std::pair{ &TerrainVegetationLayer::minimumSlope, -0.01f },
                std::pair{ &TerrainVegetationLayer::maximumSlope, 90.01f },
                std::pair{ &TerrainVegetationLayer::minimumScale, 0.0009f },
                std::pair{ &TerrainVegetationLayer::maximumScale, 1000.1f },
                std::pair{ &TerrainVegetationLayer::heightOffset, 1000.1f },
                std::pair{ &TerrainVegetationLayer::heightOffset, -1000.1f }
            };
            for (const auto& [field, value] : invalidBounds)
            {
                auto bad = settings;
                bad.enabled = false;
                bad.layers[3].*field = value;
                if (!checks.expect(!validateTerrainVegetation(bad, mesh), "numeric values just outside documented boundaries reject"))
                    return false;
            }
            ++layer.seed;
            if (!checks.expect(buildTerrainVegetation(heights, mesh, settings, repeat) && !same(saved, repeat), "changed seed changes distribution"))
                return false;
            --layer.seed;
            layer.minimumSlope = 30;
            layer.maximumSlope = 90;
            if (!checks.expect(buildTerrainVegetation(heights, mesh, settings, repeat) && repeat[0]->empty(), "slope rejects every out-of-range candidate"))
                return false;
            layer.minimumSlope = 0;
            layer.maximumSlope = 90;
            layer.minimumHeight = 0.9f;
            layer.maximumHeight = 1;
            mesh.heightScale = 0;
            mesh.heightOffset = 100000;
            if (!checks.expect(buildTerrainVegetation(heights, mesh, settings, repeat) && !repeat[0]->empty(),
                "normalized sample height remains authoritative with zero vertical scale and huge height offset"))
                return false;
            settings = recipe(L"static.model");
            settings.layers[0].maxInstances = 7;
            if (!checks.expect(buildTerrainVegetation(heights, mesh, settings, repeat) && repeat[0]->size() == 7, "explicit instance cap is exact"))
                return false;
            settings.layers[0].density = 0;
            if (!checks.expect(buildTerrainVegetation(heights, mesh, settings, repeat) && repeat[0]->empty(), "zero density yields zero instances"))
                return false;
            settings.enabled = false;
            if (!checks.expect(buildTerrainVegetation(heights, mesh, settings, repeat) && !repeat[0], "disabled vegetation has no snapshots"))
                return false;
            const std::array fields{
                &TerrainVegetationLayer::density, &TerrainVegetationLayer::minimumSpacing,
                &TerrainVegetationLayer::minimumHeight, &TerrainVegetationLayer::maximumHeight,
                &TerrainVegetationLayer::minimumSlope, &TerrainVegetationLayer::maximumSlope,
                &TerrainVegetationLayer::minimumScale, &TerrainVegetationLayer::maximumScale,
                &TerrainVegetationLayer::heightOffset
            };
            for (const auto field : fields)
                for (const float invalid : { NAN, INFINITY, -INFINITY })
                {
                    auto bad = settings;
                    bad.layers[3].*field = invalid;
                    if (!checks.expect(!buildTerrainVegetation(heights, mesh, bad, first) && same(first, saved),
                        "nonfinite inactive fields reject without changing destination"))
                        return false;
                }
            for (const auto cap : { 0u, 25001u, UINT32_MAX })
            {
                auto bad = settings;
                bad.layers[0].maxInstances = cap;
                if (!checks.expect(!buildTerrainVegetation(heights, mesh, bad, first) && same(first, saved), "invalid instance count rollback"))
                    return false;
            }
            settings = recipe(L"invalid.fbx");
            if (!checks.expect(!buildTerrainVegetation(heights, mesh, settings, first) && same(first, saved), "unsaved/import-only models rejected"))
                return false;
            settings.layers[0].modelPath = L"static.model";
            settings.layers[0].density = 100;
            if (!checks.expect(!buildTerrainVegetation(heights, mesh, settings, first) && same(first, saved), "excessive candidate budget rejects explicitly"))
                return false;
            std::atomic<bool> cancelled = true;
            settings.layers[0].density = 1;
            if (!checks.expect(!buildTerrainVegetation(heights, mesh, settings, first, &cancelled) && same(first, saved), "cancelled scatter preserves destination"))
                return false;
            Heightmap moved = std::move(heights);
            if (!checks.expect(!buildTerrainVegetation(heights, mesh, settings, first) && same(first, saved), "moved-from heightmap safely rejects"))
                return false;
            heights = std::move(moved);
            Heightmap saddle;
            const std::array<float, 4> nonplanar{ 0, 0, 0, 1 };
            auto triangleRecipe = recipe(L"triangle.model");
            triangleRecipe.layers[0].alignToNormal = true;
            TerrainVegetationPlacement grounded;
            if (!checks.expect(saddle.assign(2, 2, nonplanar)
                && buildTerrainVegetation(saddle, { 4, 4, 4, 2, 1, 1 }, triangleRecipe, grounded)
                && grounded[0]->size() > 10, "nonplanar single-cell scatter fixture"))
                return false;
            for (const auto& transform : *grounded[0])
            {
                const double u = transform._41 / 4.0 + 0.5;
                const double v = transform._43 / 4.0 + 0.5;
                const double expected = 2 + 4 * std::max(0.0, u + v - 1);
                Vector3 up(transform._21, transform._22, transform._23);
                up.Normalize();
                Vector3 expectedNormal = u + v <= 1 ? Vector3::UnitY : Vector3(-1, 1, -1);
                expectedNormal.Normalize();
                if (!checks.expect(std::abs(transform._42 - expected) < 1e-6 && up.Dot(expectedNormal) > 0.99999f,
                    "independent analytic oracle verifies exact triangle height/normal rather than bilinear grounding"))
                    return false;
            }
            auto extremeSpacing = recipe(L"spacing.model");
            extremeSpacing.layers[0].density = 100;
            extremeSpacing.layers[0].minimumSpacing = 0.1f;
            extremeSpacing.layers[0].maxInstances = 25000;
            if (!checks.expect(buildTerrainVegetation(heights, { 100000, 0.01f, 0, 0, 1, 1 }, extremeSpacing, grounded),
                "maximum aspect ratio exercises fine spatial cells at large coordinates"))
                return false;
            std::vector<Vector2> sorted;
            for (const auto& transform : *grounded[0])
                sorted.emplace_back(transform._41, transform._43);
            std::ranges::sort(sorted, {}, &Vector2::x);
            const double spacing = extremeSpacing.layers[0].minimumSpacing;
            double closestSquared = std::numeric_limits<double>::infinity();
            for (std::size_t index = 0; index < sorted.size(); ++index)
                for (std::size_t other = index + 1; other < sorted.size(); ++other)
                {
                    const double dx = static_cast<double>(sorted[other].x) - sorted[index].x;
                    if (dx * dx > closestSquared)
                        break;
                    const double dz = static_cast<double>(sorted[other].y) - sorted[index].y;
                    closestSquared = std::min(closestSquared, dx * dx + dz * dz);
                }
            if (!checks.expect(closestSquared >= spacing * spacing && closestSquared < 4 * spacing * spacing,
                "measured nearest pair at maximum coordinates satisfies the exact minimum-spacing threshold"))
                return false;
            mesh = { 100, 100, 0, 0, 1, 1 };
            settings.layers[0].density = 10;
            settings.layers[0].maxInstances = 25000;
            settings.layers[0].minimumSpacing = 0.1f;
            for (auto& item : settings.layers)
                item = settings.layers[0];
            if (!checks.expect(buildTerrainVegetation(heights, mesh, settings, repeat), "four-layer maximum bounded workload"))
                return false;
            for (const auto& transforms : repeat)
                if (!checks.expect(transforms && transforms->size() == 25000, "each layer reaches exactly 25000; total 100000 instances"))
                    return false;
            return true;
        }

        bool writeModel(Checks& checks, const std::filesystem::path& path)
        {
            Heightmap flat;
            ModelResource model;
            if (!checks.expect(buildTerrainModel(flat, { 0.5f, 0.5f, 0, 0.3f, 1, 1 }, model), "static vegetation fixture geometry"))
                return false;
            MaterialAsset canopy;
            canopy.guid = AssetGUID::generate();
            canopy.name = "Vegetation canopy";
            canopy.baseColor = Vector4(0.05f, 0.8f, 0.03f, 1);
            canopy.emissiveColor = Vector3(0.05f, 0.5f, 0.02f);
            canopy.emissiveIntensity = 1;
            canopy.roughness = 1;
            canopy.renderState.surfaceType = MaterialSurfaceType::AlphaTest;
            canopy.renderState.cullMode = MaterialCullMode::None;
            MaterialAsset trunk = canopy;
            trunk.guid = AssetGUID::generate();
            trunk.name = "Vegetation trunk";
            trunk.baseColor = Vector4(0.4f, 0.15f, 0.03f, 1);
            trunk.emissiveColor = Vector3(0.3f, 0.08f, 0.01f);
            trunk.renderState.surfaceType = MaterialSurfaceType::Opaque;
            model.embeddedMaterials = { canopy, trunk };
            model.materialSlots = { { "canopy", canopy.guid, {} }, { "trunk", trunk.guid, {} } };
            auto stem = model.meshes[0];
            for (auto& vertex : stem.vertices)
            {
                vertex.position.x *= 0.3f;
                vertex.position.z *= 0.3f;
                vertex.position.y += 0.1f;
            }
            stem.boundingBox.Center.y += 0.1f;
            stem.boundingBox.Extents.x *= 0.3f;
            stem.boundingBox.Extents.z *= 0.3f;
            stem.boundingSphere = BoundingSphere(stem.boundingBox.Center, Vector3(stem.boundingBox.Extents).Length());
            stem.subMeshes[0].materialIndex = 1;
            model.meshes.push_back(std::move(stem));
            model.nodes[0].meshIndices.push_back(1);
            AABB::CreateMerged(model.boundingBox, model.meshes[0].boundingBox, model.meshes[1].boundingBox);
            model.boundingSphere = BoundingSphere(model.boundingBox.Center, Vector3(model.boundingBox.Extents).Length());
            return checks.expect(Serialization::ModelSerializer{}.save(path, model), "save model with embedded opaque/alpha-test materials");
        }

        bool await(Checks& checks, TerrainComponent& terrain, const bool apply = true)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (terrain.getGenerationStatus().pending()
                && (apply || terrain.getGenerationStatus().stage != TerrainGenerationStage::Ready)
                && std::chrono::steady_clock::now() < deadline)
            {
                if (apply)
                    TerrainGenerationQueue::instance().applyCompleted();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto status = terrain.getGenerationStatus();
            return checks.expect(status.stage == (apply ? TerrainGenerationStage::Applied : TerrainGenerationStage::Ready)
                && status.workerThread != getCurrentThreadId(), "scatter prepares off-thread and commits only at owner barrier");
        }

        bool integration(Checks& checks, const std::filesystem::path& directory, const std::filesystem::path& modelPath)
        {
            Scene scene(ObjectGUID::generate(), "Vegetation terrain");
            auto* object = scene.createGameObject("Scatter");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            Heightmap flat;
            const auto settings = recipe(modelPath);
            if (!checks.expect(terrain->setTerrain(flat, { 8, 8, 0, 0, 1, 1 })
                && terrain->setVegetationSettings(settings) && terrain->getVegetationPlacement()[0]->size() == 64,
                "synchronous terrain and model scatter"))
                return false;
            const auto placement = terrain->getVegetationPlacement();
            const auto appliedModel = terrain->getModel();
            const auto appliedCollision = collider->getPhysicsAsset();
            scene.update(0);
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            ModelRenderSubmissionQueue::instance().consume(submissions);
            if (!checks.expect(submissions.size() == 2 && submissions[1].instanceTransforms == placement[0]
                && submissions[1].materials.size() == 2 && submissions[1].materialSnapshots->size() == 2
                && submissions[1].modelSnapshot && scene.getGameObjectCount() == 1,
                "one immutable batch per layer, embedded material snapshots, no per-plant objects"))
                return false;
            std::vector<std::uint8_t> payload, pending;
            if (!checks.expect(terrain->serializePayload(payload) && terrain->getPayloadVersion() == 7, "v7 applied vegetation payload"))
                return false;
            TerrainComponent restored, clone;
            if (!checks.expect(restored.deserializePayload(7, payload) && restored.getVegetationSettings() == settings
                && same(restored.getVegetationPlacement(), placement) && terrain->copyConfigurationTo(clone)
                && same(clone.getVegetationPlacement(), placement), "recipe load and clone reproduce exact transforms without external heightmap"))
                return false;
            for (const auto version : { 1u, 2u, 3u, 4u, 5u, 6u })
                if (!checks.expect(restored.deserializePayload(version, payload) && !restored.getVegetationSettings().enabled
                    && !restored.getVegetationPlacement()[0], "v1..6 migration defaults to disabled vegetation"))
                    return false;
            const auto* saved = Serialization::GetTerrainComponentPayload(payload.data());
            for (int invalid = 0; invalid < 3; ++invalid)
            {
                flatbuffers::FlatBufferBuilder builder;
                flatbuffers::Offset<Serialization::TerrainMaterialData> material;
                if (!Serialization::writeTerrainMaterial(builder, terrain->getMaterialSettings(), material))
                    return false;
                const auto noise = Serialization::CreateTerrainNoiseSettings(builder);
                const auto lod = Serialization::CreateTerrainLodSettings(builder);
                const auto erosion = Serialization::CreateTerrainErosionSettings(builder);
                std::vector<flatbuffers::Offset<Serialization::TerrainVegetationLayer>> layers;
                for (int index = 0; index < (invalid == 1 ? 3 : 4); ++index)
                    layers.push_back(Serialization::CreateTerrainVegetationLayer(builder, false, 0, 0, invalid == 2 ? NAN : 1));
                const auto vegetation = invalid == 0 ? flatbuffers::Offset<Serialization::TerrainVegetationSettings>{}
                    : Serialization::CreateTerrainVegetationSettings(builder, false, builder.CreateVector(layers));
                const auto samples = flat.samples();
                const auto root = Serialization::CreateTerrainComponentPayload(builder, flat.width(), flat.height(),
                    builder.CreateVector(samples.data(), samples.size()), 8, 8, 0, 0, 1, 1, true, noise, material, 64, lod, erosion, vegetation);
                Serialization::FinishTerrainComponentPayloadBuffer(builder, root);
                if (!checks.expect(!terrain->deserializePayload(7, { builder.GetBufferPointer(), builder.GetSize() })
                    && terrain->getModel() == appliedModel && same(terrain->getVegetationPlacement(), placement),
                    "missing/incorrect-count/nonfinite v7 layer payload rejects transactionally"))
                    return false;
            }
            auto invalidSettings = settings;
            invalidSettings.layers[0].minimumScale = NAN;
            if (!checks.expect(!terrain->setVegetationSettings(invalidSettings) && terrain->getModel() == appliedModel
                && collider->getPhysicsAsset() == appliedCollision && same(terrain->getVegetationPlacement(), placement),
                "invalid settings roll back mesh, PhysX and vegetation together"))
                return false;
            invalidSettings = settings;
            invalidSettings.layers[0].modelPath = directory / L"missing.model";
            if (!checks.expect(!terrain->setVegetationSettings(invalidSettings) && terrain->getModel() == appliedModel
                && same(terrain->getVegetationPlacement(), placement), "missing model never silently drops vegetation or replaces terrain"))
                return false;
            if (!checks.expect(saved->vegetation_settings() && saved->vegetation_settings()->layers()->size() == 4,
                "serialized payload contains all four applied layer recipes"))
                return false;
            Scene loaded(ObjectGUID::generate(), "Loaded vegetation"), played(ObjectGUID::generate(), "Play vegetation");
            const SceneClonePair pair{ scene, played };
            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"vegetation.scene", scene)
                && Serialization::SceneSerializer{}.load(directory / L"vegetation.scene", loaded)
                && cloneScenes(std::span(&pair, 1)), "Scene and Play serialization integration"))
                return false;
            for (const auto* copy : { &loaded, &played })
            {
                const auto* copyObject = copy->find(object->getGUID());
                const auto* copyTerrain = copyObject ? copyObject->getComponent<TerrainComponent>() : nullptr;
                if (!checks.expect(copyTerrain && copyTerrain->getVegetationSettings() == settings
                    && same(copyTerrain->getVegetationPlacement(), placement), "Scene/Play reconstruct same seeded scatter"))
                    return false;
            }
            loaded.clear();
            played.clear();
            Prefab prefab, loadedPrefab;
            Scene instances(ObjectGUID::generate(), "Vegetation prefab");
            if (!checks.expect(prefab.capture(*object) && Serialization::PrefabSerializer{}.save(directory / L"vegetation.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"vegetation.prefab", loadedPrefab), "vegetation Prefab save/load"))
                return false;
            const auto* instance = loadedPrefab.instantiate(instances);
            if (!checks.expect(instance && same(instance->getComponent<TerrainComponent>()->getVegetationPlacement(), placement),
                "Prefab reconstructs exact scatter"))
                return false;
            instances.clear();
            auto replacement = settings;
            ++replacement.layers[0].seed;
            if (!checks.expect(terrain->requestVegetation(replacement) && terrain->serializePayload(pending) && pending == payload,
                "pending scatter never leaks into saved recipe") || !await(checks, *terrain, false))
                return false;
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getVegetationSettings() == replacement && !same(terrain->getVegetationPlacement(), placement)
                && submissions[1].instanceTransforms == placement[0] && placement[0]->size() == 64,
                "atomic scatter commit retains old queued immutable instances"))
                return false;
            const auto changed = terrain->getVegetationPlacement();
            invalidSettings = settings;
            invalidSettings.layers[0].maxInstances = 0;
            if (!checks.expect(terrain->requestVegetation(settings) && !terrain->requestVegetation(invalidSettings),
                "rejected request preserves previously accepted pending scatter") || !await(checks, *terrain)
                || !checks.expect(same(terrain->getVegetationPlacement(), placement), "accepted request still applies after invalid request"))
                return false;
            if (!checks.expect(terrain->requestVegetation(replacement), "queue cancellable vegetation") || !await(checks, *terrain, false))
                return false;
            terrain->cancelGeneration();
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(same(terrain->getVegetationPlacement(), placement), "cancelled ready result cannot replace scatter")
                || !checks.expect(terrain->requestVegetation(replacement) && terrain->requestVegetation(settings), "latest accepted scatter wins")
                || !await(checks, *terrain) || !checks.expect(same(terrain->getVegetationPlacement(), placement), "older queued scatter never publishes"))
                return false;
            invalidSettings = settings;
            invalidSettings.layers[0].modelPath = directory / L"async-missing.model";
            if (!checks.expect(terrain->requestVegetation(invalidSettings), "valid path shape queues before missing asset resolution")
                || !await(checks, *terrain, false))
                return false;
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && same(terrain->getVegetationPlacement(), placement), "missing async asset explicitly fails and preserves applied scatter"))
                return false;
            if (!checks.expect(terrain->requestFlat(17, 17, 0.7f, { 8, 8, 0, 2, 1, 1 }), "height change captures applied scatter recipe")
                || !await(checks, *terrain))
                return false;
            const auto raised = terrain->getVegetationPlacement();
            if (!checks.expect(raised[0]->size() == placement[0]->size(), "terrain edit retains deterministic candidate count"))
                return false;
            for (std::size_t index = 0; index < raised[0]->size(); ++index)
            {
                const auto& a = (*placement[0])[index];
                const auto& b = (*raised[0])[index];
                if (!checks.expect(a._41 == b._41 && a._43 == b._43 && b._42 == 2, "terrain regeneration re-grounds every unchanged seeded XZ root"))
                    return false;
                const auto hit = PhysicsSystem::instance().getWorld()->raycast(Vector3(b._41, 10, b._43), -Vector3::UnitY, 20);
                if (!checks.expect(hit && hit->collider == collider && std::abs(hit->point.y - 2) < 0.001f,
                    "PhysX remains terrain-only; no collision is created for canopy instances"))
                    return false;
            }
            if (!checks.expect(terrain->setChunkSettings({ 16 }) && terrain->setLodSettings({ true, 4, 3 })
                && same(terrain->getVegetationPlacement(), raised), "chunking and LOD never alter scatter"))
                return false;
            if (!checks.expect(terrain->requestVegetation(replacement), "queue vegetation for busy-physics rollback")
                || !await(checks, *terrain, false))
                return false;
            auto* world = PhysicsSystem::instance().getWorld();
            world->simulate(1.0f / 60);
            TerrainGenerationQueue::instance().applyCompleted();
            world->fetchResults();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && same(terrain->getVegetationPlacement(), raised), "failed PhysX transaction never publishes prepared vegetation"))
                return false;
            auto heavy = settings;
            for (auto& layer : heavy.layers)
            {
                layer = heavy.layers[0];
                layer.density = 100;
                layer.minimumSpacing = 0.1f;
                layer.maxInstances = 25000;
            }
            if (!checks.expect(terrain->requestVegetation(heavy), "queue candidate-heavy scatter for in-flight cancellation"))
                return false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (terrain->getGenerationStatus().stage != TerrainGenerationStage::Vegetation
                && terrain->getGenerationStatus().pending() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Vegetation,
                "observe actual worker scatter stage before cancelling"))
                return false;
            terrain->cancelGeneration();
            if (!checks.expect(terrain->requestVegetation(replacement), "worker remains reusable after cancelling active candidates")
                || !await(checks, *terrain))
                return false;
            if (!checks.expect(terrain->setVegetationSettings(settings) && same(terrain->getVegetationPlacement(), raised),
                "restore known grounded scatter after cancellation"))
                return false;
            if (!checks.expect(terrain->requestVegetation(replacement), "queue scatter before disabling") || !await(checks, *terrain, false))
                return false;
            terrain->setEnabled(false);
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(same(terrain->getVegetationPlacement(), raised), "disable prevents completion from publishing"))
                return false;
            terrain->setEnabled(true);
            if (!checks.expect(terrain->requestVegetation(replacement), "queue scatter before destruction") || !await(checks, *terrain, false))
                return false;
            scene.clear();
            TerrainGenerationQueue::instance().applyCompleted();
            return checks.expect(changed[0] && changed[0]->size() == 64 && placement[0]->size() == 64,
                "destroyed owner cancels callbacks; shared earlier placement snapshots remain valid");
        }

        RenderView overview()
        {
            RenderView view;
            view.camera.position = Vector3(0, 20, 0);
            view.camera.forward = -Vector3::UnitY;
            view.camera.view = Matrix(DirectX::XMMatrixLookAtLH(view.camera.position, Vector3::Zero, Vector3::UnitZ));
            view.camera.projection = Matrix(DirectX::XMMatrixOrthographicLH(10, 10, 0.1f, 100));
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

        bool draw(DX12Renderer& renderer, const std::vector<ModelRenderSubmission>& submissions, const RenderView& view)
        {
            for (const auto& submission : submissions)
                ModelRenderSubmissionQueue::instance().submit(submission);
            CameraRenderSubmissionQueue::instance().submit(view);
            const auto frame = FrameProfiler::instance().beginFrame();
            if (!frame)
                return false;
            const bool rendered = renderer.render();
            return FrameProfiler::instance().endFrame(frame, 1) && rendered;
        }

        bool equalPixels(const DX12FrameCapture& a, const DX12FrameCapture& b)
        {
            const auto* x = a.display.GetImage(0, 0, 0);
            const auto* y = b.display.GetImage(0, 0, 0);
            return x && y && x->slicePitch == y->slicePitch && std::memcmp(x->pixels, y->pixels, x->slicePitch) == 0;
        }

        std::vector<ModelRenderSubmission> expand(const std::vector<ModelRenderSubmission>& submissions)
        {
            std::vector<ModelRenderSubmission> expanded;
            for (const auto& source : submissions)
            {
                if (!source.instanceTransforms)
                {
                    expanded.push_back(source);
                    continue;
                }
                for (const auto& local : *source.instanceTransforms)
                {
                    auto individual = source;
                    individual.instanceTransforms.reset();
                    individual.worldMatrix = local * source.worldMatrix;
                    individual.modelSnapshot->boundingBox.Transform(individual.worldBounds, individual.worldMatrix);
                    expanded.push_back(std::move(individual));
                }
            }
            return expanded;
        }

        bool rendering(Checks& checks, DX12Renderer& renderer, const std::filesystem::path& directory,
            const std::filesystem::path& modelPath)
        {
            Scene scene(ObjectGUID::generate(), "Vegetation rendering"), empty(ObjectGUID::generate(), "No lights");
            LightingFrame::instance().capture(nullptr, empty);
            auto* object = scene.createGameObject("Vegetation");
            auto* terrain = object->addComponent<TerrainComponent>();
            Heightmap flat;
            if (!checks.expect(terrain->setTerrain(flat, { 8, 8, 0, 0, 1, 1 })
                && terrain->setVegetationSettings(recipe(modelPath)), "vegetation render fixture") || !renderer.resize(256, 256))
                return false;
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            ModelRenderSubmissionQueue::instance().consume(submissions);
            if (!checks.expect(submissions.size() == 2, "GPU test receives compact terrain + vegetation submissions"))
                return false;
            auto instances = submissions[1];
            auto expanded = expand(submissions);
            std::ofstream metrics(directory / L"vegetation-draws.txt");
            renderer.setEditorOverlayEnabled(false);
            renderer.setOcclusionCullingEnabled(false);
            const auto view = overview();
            for (const bool deferred : { true, false })
            {
                renderer.setDeferredRenderingEnabled(deferred);
                renderer.setModelInstancingEnabled(true);
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(draw(renderer, submissions, view), "draw compact vegetation batches"))
                        return false;
                DX12FrameCapture batched, individual, background;
                if (!checks.expect(renderer.captureFrame(batched), "capture instanced vegetation"))
                    return false;
                const auto batchedStatistics = renderer.getStatistics();
                if (!checks.expect(batchedStatistics.visibleObjects == 65 && batchedStatistics.instanceCount >= 129
                    && batchedStatistics.drawCallCount < 20, "real opaque/alpha-test instances share a bounded number of GPU draws"))
                    return false;
                if (deferred && !checks.expect(batched.save(directory, L"terrain-vegetation"), "save actual vegetation GPU capture"))
                    return false;
                renderer.setModelInstancingEnabled(false);
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(draw(renderer, expanded, view), "draw individually expanded oracle instances"))
                        return false;
                if (!checks.expect(renderer.captureFrame(individual) && equalPixels(batched, individual),
                    "Deferred/Forward compact batches match noninstanced oracle pixels exactly"))
                    return false;
                if (!checks.expect(renderer.getStatistics().drawCallCount > batchedStatistics.drawCallCount * 8,
                    "instancing reduces actual GPU Draw calls, not just submission count"))
                    return false;
                metrics << (deferred ? "Deferred" : "Forward") << ": compact_submissions=" << submissions.size()
                    << " expanded_submissions=" << expanded.size() << " instances_per_layer=" << instances.instanceTransforms->size()
                    << " batched_draw_calls=" << batchedStatistics.drawCallCount
                    << " individual_draw_calls=" << renderer.getStatistics().drawCallCount << '\n';
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(draw(renderer, { submissions[0] }, view), "draw terrain without vegetation"))
                        return false;
                if (!checks.expect(renderer.captureFrame(background) && !equalPixels(batched, background),
                    "vegetation produces measurable visible GPU pixels, not just queue records"))
                    return false;
                auto masked = view;
                masked.cullingMask = 0;
                if (!checks.expect(draw(renderer, submissions, masked) && renderer.captureFrame(individual)
                    && draw(renderer, {}, masked) && renderer.captureFrame(background) && equalPixels(individual, background),
                    "camera layer masks exclude all terrain vegetation instances"))
                    return false;
                renderer.setModelInstancingEnabled(true);
                renderer.setOcclusionCullingEnabled(true);
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(draw(renderer, submissions, view), "draw vegetation through GPU occlusion compaction"))
                        return false;
                if (!checks.expect(renderer.captureFrame(individual) && equalPixels(batched, individual), "Hi-Z preserves visible vegetation pixels"))
                    return false;
                renderer.setOcclusionCullingEnabled(false);
            }
            auto* parent = scene.createGameObject("Scatter parent");
            parent->getTransform()->setScale(Vector3(-1.1f, 1.3f, 1));
            parent->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(0.35f, 0, 0));
            parent->getTransform()->setPosition(Vector3(0.3f, 0.1f, 0.2f));
            if (!checks.expect(object->setParent(parent, false), "parent terrain without preserving world transform"))
                return false;
            object->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(0.5f, 0.15f, 0));
            object->getTransform()->setScale(Vector3(0.8f, 1.2f, 0.9f));
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> transformed;
            ModelRenderSubmissionQueue::instance().consume(transformed);
            if (!checks.expect(transformed.size() == 2 && transformed[1].instanceTransforms == instances.instanceTransforms,
                "parent reflection/rotation/nonuniform scale never regenerates local placement"))
                return false;
            expanded = expand(transformed);
            for (const bool deferred : { true, false })
            {
                renderer.setDeferredRenderingEnabled(deferred);
                renderer.setModelInstancingEnabled(true);
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(draw(renderer, transformed, view), "draw parent-transformed vegetation"))
                        return false;
                DX12FrameCapture batched, oracle;
                if (!checks.expect(renderer.captureFrame(batched), "capture transformed vegetation"))
                    return false;
                renderer.setModelInstancingEnabled(false);
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(draw(renderer, expanded, view), "draw transformed individual oracle"))
                        return false;
                if (!checks.expect(renderer.captureFrame(oracle) && equalPixels(batched, oracle),
                    "parent-composed instances match individual oracle through reflection/nonuniform shear in both paths"))
                    return false;
            }
            renderer.setModelInstancingEnabled(true);
            renderer.setDeferredRenderingEnabled(true);
            auto* lightObject = scene.createGameObject("Vegetation shadows");
            auto* light = lightObject->addComponent<LightComponent>();
            LightSettings lighting;
            lighting.castsShadow = true;
            lightObject->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(0, DirectX::XM_PIDIV2, 0));
            if (!checks.expect(light->setSettings(lighting), "directional vegetation shadow fixture"))
                return false;
            LightingFrame::instance().capture(nullptr, scene);
            auto outside = view;
            const auto distantView = Matrix(DirectX::XMMatrixLookAtLH(Vector3(100, 20, 0), Vector3(100, 0, 0), Vector3::UnitZ));
            outside.frustum = CameraUtils::buildWorldFrustum(view.camera.projection, distantView.Invert());
            if (!checks.expect(draw(renderer, { instances }, outside) && renderer.getStatistics().visibleObjects == 0
                && renderer.getStatistics().drawCallCount > 0, "off-camera vegetation still draws in independent light frustum"))
                return false;
            auto noShadows = instances;
            noShadows.castShadows = false;
            if (!checks.expect(draw(renderer, { noShadows }, outside) && renderer.getStatistics().drawCallCount == 0,
                "vegetation shadow toggle is independent and disables off-camera shadow draws"))
                return false;
            LightingFrame::instance().capture(nullptr, empty);
            scene.clear();
            ModelManager::instance().unload(instances.model);
            for (const auto material : instances.materials)
                MaterialManager::instance().unload(material);
            renderer.setDeferredRenderingEnabled(true);
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(draw(renderer, submissions, view), "queued immutable model/material/instance data outlives scene and manager handles"))
                    return false;
            metrics.close();
            return checks.expect(static_cast<bool>(metrics), "write actual vegetation GPU draw-call measurements")
                && checks.expect(renderer.validateDebugMessages(), "vegetation rendering produces no D3D12 diagnostics");
        }
    }

    bool validateTerrainVegetationFeature(DX12Renderer& renderer, const std::filesystem::path& directory, std::size_t& count)
    {
        Checks checks;
        const auto modelPath = std::filesystem::absolute(directory / L"vegetation.model").lexically_normal();
        if (!pure(checks) || !writeModel(checks, modelPath) || !integration(checks, directory, modelPath)
            || !rendering(checks, renderer, directory, modelPath))
            return false;
        count = checks.count;
        LOG_INFO("[TerrainVegetationValidation] PASS: {} checks.", count);
        return true;
    }
}
