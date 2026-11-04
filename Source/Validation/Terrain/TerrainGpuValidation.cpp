#include "Pch.h"
#include "Validation\Terrain\TerrainGpuValidation.h"
#include "Terrain\Generation\TerrainGpuNoise.h"
#include "Components\Terrain\TerrainComponent.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Threading\ThreadUtility.h"
#include "Assets\Model\ModelManager.h"
#include "Components\Physics\Collider\TerrainColliderComponent.h"
#include "Physics\PhysicsMeshAsset.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include <cmath>

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
                    LOG_ERROR("[TerrainGpuValidation] FAILED: {}.", label);
                else
                    ++count;
                return value;
            }
        };

        bool same(const Heightmap& a, const Heightmap& b)
        {
            return a.width() == b.width() && a.height() == b.height() && std::ranges::equal(a.samples(), b.samples());
        }

        bool await(Checks& checks, const TerrainComponent& terrain, const bool apply = true)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
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
                && status.workerThread != 0 && status.workerThread != getCurrentThreadId()
                && (!apply || status.applyThread == getCurrentThreadId()), "GPU worker completes without renderer-frame pumping; owner alone commits");
        }

        bool numerical(Checks& checks, const std::filesystem::path& directory)
        {
            auto& gpu = TerrainGpuNoise::instance();
            if (!checks.expect(gpu.isAvailable(), "renderer initializes independent GPU noise backend"))
                return false;
            std::vector<TerrainNoiseSettings> cases;
            TerrainNoiseSettings settings;
            settings.width = 65;
            settings.height = 41;
            settings.seed = 987;
            settings.offsetX = -2.37f;
            cases.push_back(settings);
            settings.mode = TerrainNoiseMode::Perlin;
            cases.push_back(settings);
            settings.mode = TerrainNoiseMode::FBM;
            settings.octaves = 1;
            cases.push_back(settings);
            settings.octaves = 12;
            settings.persistence = 0;
            cases.push_back(settings);
            settings.persistence = 1;
            settings.lacunarity = 4;
            settings.frequency = 1024;
            settings.offsetX = -100000;
            settings.offsetZ = 100000;
            settings.seed = UINT32_MAX;
            cases.push_back(settings);
            settings.lacunarity = 1;
            settings.frequency = 0.001f;
            settings.offsetX = -0.0007f;
            settings.offsetZ = 0.0003f;
            cases.push_back(settings);
            for (const auto dimensions : { std::pair{ 2u, 2u }, { 2u, 37u }, { 43u, 2u }, { 7u, 9u }, { 1025u, 1025u } })
            {
                settings = {};
                settings.width = dimensions.first;
                settings.height = dimensions.second;
                settings.seed = 723;
                settings.offsetX = -3.27f;
                settings.offsetZ = 8.19f;
                settings.octaves = 12;
                cases.push_back(settings);
            }
            settings = cases.back();
            settings.width = 67;
            settings.height = 39;
            settings.frequency = 1023.17f;
            settings.offsetX = -99999.83f;
            settings.offsetZ = 99999.71f;
            settings.lacunarity = 3.91f;
            settings.persistence = 0.97f;
            cases.push_back(settings);
            double maximumError = 0, cpuMs = 0, gpuMs = 0;
            for (const auto& recipe : cases)
            {
                Heightmap cpu, result, repeat;
                const auto start = std::chrono::steady_clock::now();
                if (!checks.expect(generateTerrainNoise(recipe, cpu), "CPU double noise oracle"))
                    return false;
                const auto cpuEnd = std::chrono::steady_clock::now();
                if (!checks.expect(gpu.generate(recipe, result), "actual GPU compute/readback"))
                    return false;
                const auto gpuEnd = std::chrono::steady_clock::now();
                if (recipe.width == 1025 && recipe.height == 1025)
                {
                    cpuMs = std::chrono::duration<double, std::milli>(cpuEnd - start).count();
                    gpuMs = std::chrono::duration<double, std::milli>(gpuEnd - cpuEnd).count();
                }
                if (!checks.expect(gpu.generate(recipe, repeat) && same(result, repeat)
                    && result.width() == recipe.width && result.height() == recipe.height,
                    "same GPU recipe repeats exact samples and retains rectangular dimensions"))
                    return false;
                for (std::size_t i = 0; i < result.samples().size(); ++i)
                {
                    const float value = result.samples()[i];
                    const double error = std::abs(static_cast<double>(value) - cpu.samples()[i]);
                    maximumError = std::max(maximumError, error);
                    if (!checks.expect(std::isfinite(value) && value >= 0 && value <= 1 && error <= 0.000002,
                        "every GPU sample is normalized and within 2e-6 of CPU including extreme coordinates"))
                    {
                        LOG_ERROR("[TerrainGpuValidation] Sample {}: CPU={}, GPU={}, error={}.", i, cpu.samples()[i], value, error);
                        return false;
                    }
                }
            }
            Heightmap result, equivalent;
            auto recipe = cases[1];
            if (!checks.expect(gpu.generate(recipe, result), "Perlin parity fixture"))
                return false;
            recipe.mode = TerrainNoiseMode::FBM;
            recipe.octaves = 1;
            if (!checks.expect(gpu.generate(recipe, equivalent) && same(result, equivalent), "GPU FBM one octave exactly equals Perlin"))
                return false;
            recipe.octaves = 12;
            recipe.persistence = 0;
            if (!checks.expect(gpu.generate(recipe, equivalent) && same(result, equivalent), "GPU zero persistence exactly equals Perlin"))
                return false;
            recipe.seed++;
            if (!checks.expect(gpu.generate(recipe, equivalent) && !same(result, equivalent), "GPU seed changes heights"))
                return false;
            const auto previous = equivalent;
            std::atomic<bool> cancelled = true;
            if (!checks.expect(!gpu.generate(recipe, equivalent, &cancelled) && same(previous, equivalent),
                "pre-cancelled GPU request leaves destination unchanged"))
                return false;
            recipe.width = 1;
            if (!checks.expect(!gpu.generate(recipe, equivalent) && same(previous, equivalent), "invalid GPU grid preserves destination"))
                return false;
            recipe = {};
            recipe.offsetZ = NAN;
            if (!checks.expect(!gpu.generate(recipe, equivalent) && same(previous, equivalent), "nonfinite GPU recipe rejected before submission"))
                return false;
            recipe = cases[10];
            std::atomic<TerrainGenerationStage> stage{ TerrainGenerationStage::GpuNoise };
            std::atomic<bool> finished = false;
            cancelled.store(false, std::memory_order_release);
            bool generated = false;
            std::jthread worker([&]
            {
                generated = gpu.generate(recipe, equivalent, &cancelled, &stage);
                finished.store(true, std::memory_order_release);
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (stage.load(std::memory_order_acquire) != TerrainGenerationStage::GpuReadback
                && !finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            cancelled.store(true, std::memory_order_release);
            worker.join();
            if (!checks.expect(stage.load(std::memory_order_acquire) == TerrainGenerationStage::GpuReadback
                && !generated && same(previous, equivalent), "cancellation after actual GPU submission retires work without publishing heights"))
                return false;
            if (!checks.expect(gpu.generate(recipe, equivalent), "compute allocator/upload/output/readback safely reuse after in-flight cancellation"))
                return false;
            std::ofstream metrics(directory / L"gpu-noise-timing.txt");
            metrics << "Actual GPU dispatch + independent fence + readback; not GPU timestamp-only\n"
                << "max_normalized_error=" << maximumError << "\nmax_grid_cpu_ms=" << cpuMs
                << "\nmax_grid_gpu_with_readback_ms=" << gpuMs << "\nrecipes=" << cases.size() << '\n';
            metrics.close();
            LOG_INFO("[TerrainGpuValidation] Maximum sample error={}, maximum-grid CPU={}ms, GPU+readback={}ms.",
                maximumError, cpuMs, gpuMs);
            return checks.expect(static_cast<bool>(metrics), "write actual GPU error/timing measurements");
        }

        bool integration(Checks& checks, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "GPU noise");
            auto* object = scene.createGameObject("GPU terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            TerrainNoiseSettings recipe;
            recipe.width = 41;
            recipe.height = 29;
            recipe.seed = 198;
            recipe.offsetX = -2.37f;
            TerrainMeshSettings mesh{ 40, 28, 4, -1, 3, 5 };
            TerrainMaterialSettings material;
            material.enabled = true;
            if (!checks.expect(terrain->setChunkSettings({ 16 }) && terrain->setMaterialSettings(material)
                && terrain->setLodSettings({ true, 4, 17 }), "GPU terrain layered/LOD/PhysX fixture"))
                return false;
            scene.update(0);
            const auto oldModel = terrain->getModel();
            const auto oldCollision = collider->getPhysicsAsset();
            TerrainBuildRequest invalidSource;
            invalidSource.gpuNoise = true;
            if (!checks.expect(!validateTerrainBuildRequest(invalidSource), "GPU flag cannot silently change a heightmap source into noise"))
                return false;
            std::vector<std::uint8_t> before, pending, payload;
            if (!checks.expect(terrain->serializePayload(before) && terrain->requestGpuNoise(recipe, mesh)
                && terrain->serializePayload(pending) && before == pending && terrain->getModel() == oldModel
                && collider->getPhysicsAsset() == oldCollision, "accepted GPU request leaves authoritative terrain and saved data unchanged")
                || !await(checks, *terrain, false))
                return false;
            TerrainComponent clone;
            if (!checks.expect(terrain->copyConfigurationTo(clone) && clone.serializePayload(pending) && pending == before,
                "clone never copies unapplied GPU request"))
                return false;
            auto invalid = recipe;
            invalid.frequency = INFINITY;
            if (!checks.expect(!terrain->requestGpuNoise(invalid, mesh)
                && terrain->getGenerationStatus().stage == TerrainGenerationStage::Ready, "invalid GPU replacement preserves Ready work")
                || !await(checks, *terrain))
                return false;
            Heightmap readback;
            if (!checks.expect(TerrainGpuNoise::instance().generate(recipe, readback)
                && same(terrain->getHeightmap(), readback) && terrain->getNoiseSettings() == recipe,
                "applied GPU samples and authoring recipe exactly match real readback"))
                return false;
            const auto model = ModelManager::instance().get(terrain->getModel());
            TerrainBuildRequest request;
            request.heightmap = readback;
            request.mesh = mesh;
            request.material = material;
            request.chunks = { 16 };
            request.lod = { true, 4, 17 };
            request.collision = true;
            TerrainBuildResult oracle;
            if (!checks.expect(buildTerrain(request, oracle) && model && model->meshes.size() == oracle.model.meshes.size()
                && collider->getChunkAssets().size() == oracle.collision.size(), "GPU readback feeds unchanged CPU mesh and collision pipeline"))
                return false;
            for (std::size_t chunk = 0; chunk < model->meshes.size(); ++chunk)
            {
                const auto& a = model->meshes[chunk];
                const auto& b = oracle.model.meshes[chunk];
                if (!checks.expect(a.indices == b.indices && a.vertices.size() == b.vertices.size()
                    && std::ranges::equal(collider->getChunkAssets()[chunk]->getCookedData(), oracle.collision[chunk]->getCookedData()),
                    "GPU terrain indices and full-resolution cooked PhysX equal readback oracle"))
                    return false;
                for (std::size_t index = 0; index < a.vertices.size(); ++index)
                {
                    const auto& p = a.vertices[index];
                    const auto& q = b.vertices[index];
                    if (!checks.expect(p.position == q.position && p.normal == q.normal && p.tangent == q.tangent
                        && p.bitangent == q.bitangent && p.texCoord == q.texCoord && p.color == q.color,
                        "every GPU terrain vertex/normal/tangent/layer/UV matches authoritative readback"))
                        return false;
                }
            }
            auto* world = PhysicsSystem::instance().getWorld();
            for (int z = -6; z <= 6; ++z)
                for (int x = -9; x <= 9; ++x)
                {
                    const Vector3 position(x * 2.0f + 0.13f, 20, z * 2.0f + 0.31f);
                    const auto hit = world->raycast(position, -Vector3::UnitY, 40);
                    const auto surface = terrain->sampleWorldSurface(position);
                    if (!checks.expect(hit && surface && hit->collider == collider
                        && std::abs(hit->point.y - surface->position.y) < 0.001f && hit->normal.Dot(surface->normal) > 0.999f,
                        "GPU-derived full-resolution PhysX heights/normals match CPU surface queries"))
                        return false;
                }
            TerrainComponent restored;
            if (!checks.expect(terrain->serializePayload(payload) && restored.deserializePayload(6, payload)
                && same(restored.getHeightmap(), readback) && restored.getNoiseSettings() == recipe
                && restored.setTerrain(restored.getHeightmap(), restored.getSettings()) && same(restored.getHeightmap(), readback),
                "GPU samples save/load/rebuild without replay or CPU regeneration"))
                return false;
            Scene loaded(ObjectGUID::generate(), "Loaded GPU"), cloned(ObjectGUID::generate(), "GPU Play");
            const SceneClonePair pair{ scene, cloned };
            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"gpu-noise.scene", scene)
                && Serialization::SceneSerializer{}.load(directory / L"gpu-noise.scene", loaded)
                && cloneScenes(std::span(&pair, 1)), "GPU terrain Scene/Play roundtrip"))
                return false;
            for (const auto* copy : { &loaded, &cloned })
            {
                const auto* copyObject = copy->find(object->getGUID());
                const auto* copyTerrain = copyObject ? copyObject->getComponent<TerrainComponent>() : nullptr;
                if (!checks.expect(copyTerrain && copyTerrain->getNoiseSettings() == recipe && same(copyTerrain->getHeightmap(), readback),
                    "GPU terrain Scene/Play copies exact readback samples"))
                    return false;
            }
            loaded.clear();
            cloned.clear();
            Prefab prefab, loadedPrefab;
            Scene instances(ObjectGUID::generate(), "GPU prefab");
            if (!checks.expect(prefab.capture(*object) && Serialization::PrefabSerializer{}.save(directory / L"gpu-noise.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"gpu-noise.prefab", loadedPrefab), "GPU terrain Prefab roundtrip"))
                return false;
            auto* instance = loadedPrefab.instantiate(instances);
            const auto* instanceTerrain = instance ? instance->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(instanceTerrain && same(instanceTerrain->getHeightmap(), readback), "GPU Prefab instantiates without replay"))
                return false;
            instances.clear();
            const auto applied = terrain->getModel();
            const auto asset = collider->getPhysicsAsset();
            recipe.seed++;
            if (!checks.expect(terrain->requestGpuNoise(recipe, mesh), "queue GPU busy-physics replacement") || !await(checks, *terrain, false))
                return false;
            world->simulate(1.0f / 60);
            TerrainGenerationQueue::instance().applyCompleted();
            world->fetchResults();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && terrain->getModel() == applied && collider->getPhysicsAsset() == asset
                && same(terrain->getHeightmap(), readback), "GPU result cannot partially apply while PhysX is busy"))
                return false;
            if (!checks.expect(terrain->requestGpuNoise(recipe, mesh), "queue GPU Ready cancellation") || !await(checks, *terrain, false))
                return false;
            terrain->cancelGeneration();
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Cancelled && terrain->getModel() == applied,
                "Ready GPU cancellation preserves old geometry"))
                return false;
            recipe.width = recipe.height = 1025;
            recipe.octaves = 12;
            if (!checks.expect(terrain->requestGpuNoise(recipe, mesh), "queue maximum GPU cancellation"))
                return false;
            terrain->cancelGeneration();
            if (!checks.expect(terrain->requestFlat(17, 9, 0.37f, mesh), "CPU request supersedes GPU work") || !await(checks, *terrain))
                return false;
            if (!checks.expect(terrain->getHeightmap().width() == 17 && terrain->getHeightmap().height() == 9
                && terrain->getHeightmap().samples()[0] == 0.37f, "late GPU completion cannot override newer CPU request"))
                return false;
            TerrainComponent independent;
            recipe.width = 33;
            recipe.height = 19;
            if (!checks.expect(independent.requestGpuNoise(recipe, mesh) && terrain->requestGpuNoise(recipe, mesh),
                "independent components share bounded compute resources") || !await(checks, independent) || !await(checks, *terrain))
                return false;
            if (!checks.expect(same(independent.getHeightmap(), terrain->getHeightmap()), "independent GPU requests reproduce same data"))
                return false;
            {
                TerrainComponent removed;
                if (!checks.expect(removed.requestGpuNoise(recipe, mesh), "GPU component destruction fixture") || !await(checks, removed, false))
                    return false;
            }
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->requestGpuNoise(recipe, mesh), "GPU worker shutdown fixture"))
                return false;
            TerrainGenerationQueue::instance().shutdown();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Cancelled
                && independent.requestGpuNoise(recipe, mesh), "worker joins/drains and restarts without losing backend"))
                return false;
            return await(checks, independent);
        }
    }

    bool validateTerrainGpu(const std::filesystem::path& directory, std::size_t& passed)
    {
        passed = 0;
        Checks checks;
        if (!numerical(checks, directory) || !integration(checks, directory))
            return false;
        passed = checks.count;
        LOG_INFO("[TerrainGpuValidation] PASS: {} checks.", passed);
        return true;
    }
}
