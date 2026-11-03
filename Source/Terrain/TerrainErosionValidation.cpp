#include "Pch.h"
#include "Terrain\TerrainErosionValidation.h"
#include "Core\GameObject\Component\TerrainComponent.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneSerializer.h"
#include "Assets\Model\ModelManager.h"
#include "Generated\FlatBuffers\TerrainComponent_generated.h"
#include "Terrain\TerrainMaterialSerialization.h"
#include "Physics\Collider\TerrainColliderComponent.h"
#include "Physics\Character\CharacterControllerComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\PhysicsMeshAsset.h"
#include <cmath>
#include <numeric>
#include <tuple>

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
                    LOG_ERROR("[TerrainErosionValidation] FAILED: {}.", label);
                else
                    ++count;
                return value;
            }
        };

        bool same(const Heightmap& a, const Heightmap& b)
        {
            return a.width() == b.width() && a.height() == b.height()
                && std::ranges::equal(a.samples(), b.samples());
        }

        bool normalized(const Heightmap& map)
        {
            return std::ranges::all_of(map.samples(), [](const float value)
            {
                return std::isfinite(value) && value >= 0 && value <= 1;
            });
        }

        bool await(Checks& checks, const TerrainComponent& terrain, const bool apply = true)
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
            return checks.expect(terrain.getGenerationStatus().stage
                == (apply ? TerrainGenerationStage::Applied : TerrainGenerationStage::Ready), "erosion completes at requested barrier");
        }

        bool budgetMatches(Checks& checks, const Heightmap& before, const Heightmap& after, const TerrainErosionBudget& budget)
        {
            const double initial = std::accumulate(before.samples().begin(), before.samples().end(), 0.0);
            const double finalValue = std::accumulate(after.samples().begin(), after.samples().end(), 0.0);
            return checks.expect(normalized(after) && before.width() == after.width() && before.height() == after.height(),
                "erosion retains dimensions and finite normalized range")
                && checks.expect(budget.eroded >= 0 && budget.deposited >= 0 && budget.exported >= 0
                    && std::abs(budget.eroded - budget.deposited - budget.exported) < 1e-8 * std::max(1.0, budget.eroded),
                    "eroded sediment equals deposited plus exported sediment")
                && checks.expect(std::abs(initial - finalValue - budget.exported) < before.samples().size() * 6e-8 + 1e-8,
                    "actual output sample mass agrees with open-boundary export within float rounding");
        }

        bool algorithm(Checks& checks, const std::filesystem::path& directory)
        {
            Heightmap source, result, repeat;
            TerrainNoiseSettings noise;
            noise.width = 65;
            noise.height = 41;
            noise.seed = 987;
            noise.offsetX = -2.37f;
            TerrainErosionSettings settings;
            settings.seed = 723;
            TerrainErosionBudget budget;
            if (!checks.expect(generateTerrainNoise(noise, source), "erosion source noise"))
                return false;
            const auto start = std::chrono::steady_clock::now();
            if (!checks.expect(erodeTerrain(source, settings, result, nullptr, &budget)
                && erodeTerrain(source, settings, repeat) && same(result, repeat), "seeded erosion is exactly reproducible")
                || !budgetMatches(checks, source, result, budget))
                return false;
            const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 2;
            const auto measuredBudget = budget;
            std::size_t changed = 0;
            for (std::size_t i = 0; i < source.samples().size(); ++i)
                if (std::abs(source.samples()[i] - result.samples()[i]) > 0.00001f)
                    ++changed;
            if (!checks.expect(changed > source.samples().size() / 2 && budget.eroded > 1
                && budget.deposited > 1 && budget.exported > 0
                && budget.exported < std::accumulate(source.samples().begin(), source.samples().end(), 0.0) * 0.5,
                "default recipe transports meaningful sediment without stripping most fixture height mass"))
                return false;
            ++settings.seed;
            if (!checks.expect(erodeTerrain(source, settings, repeat) && !same(result, repeat), "seed changes erosion paths"))
                return false;
            repeat = source;
            --settings.seed;
            if (!checks.expect(erodeTerrain(repeat, settings, repeat) && same(result, repeat), "aliased source/destination exact parity"))
                return false;
            for (const float height : { 0.0f, 0.5f, 1.0f })
                if (!checks.expect(source.createFlat(17, 9, height) && erodeTerrain(source, settings, result, nullptr, &budget)
                    && same(source, result) && budget.eroded == 0 && budget.deposited == 0 && budget.exported == 0,
                    "flat terrain is invariant without artificial flow or normalization"))
                    return false;
            if (!checks.expect(generateTerrainNoise(noise, source), "restore rough erosion fixture"))
                return false;
            auto disabled = settings;
            disabled.erosionRate = 0;
            if (!checks.expect(erodeTerrain(source, disabled, result) && same(source, result), "zero erosion rate preserves heights"))
                return false;
            disabled = settings;
            disabled.inertia = 1;
            if (!checks.expect(erodeTerrain(source, disabled, result) && same(source, result), "full inertia with zero direction does not invent flow"))
                return false;
            for (const auto dimensions : { std::pair{ 2u, 2u }, { 2u, 31u }, { 47u, 2u }, { 19u, 7u }, { 1025u, 1025u } })
            {
                std::vector<float> samples(static_cast<std::size_t>(dimensions.first) * dimensions.second);
                for (std::size_t i = 0; i < samples.size(); ++i)
                    samples[i] = i % 2 ? 1.0f : 0.0f;
                auto extreme = settings;
                extreme.droplets = 1000;
                extreme.maxLifetime = 256;
                extreme.radius = 8;
                extreme.capacity = 100;
                extreme.gravity = 100;
                extreme.initialWater = 10;
                extreme.initialSpeed = 10;
                extreme.erosionRate = extreme.depositRate = 1;
                extreme.evaporation = 0;
                if (!checks.expect(source.assign(dimensions.first, dimensions.second, samples)
                    && erodeTerrain(source, extreme, result, nullptr, &budget), "extreme cliff/tiny/rectangular/maximum grid erosion")
                    || !budgetMatches(checks, source, result, budget))
                    return false;
                extreme.radius = 1;
                extreme.evaporation = 1;
                extreme.gravity = extreme.initialSpeed = 0;
                if (!checks.expect(erodeTerrain(source, extreme, result, nullptr, &budget), "unit brush/immediate evaporation/zero energy"))
                    return false;
                if (!budgetMatches(checks, source, result, budget))
                    return false;
            }
            std::ofstream metrics(directory / L"erosion-budget.txt");
            metrics << "65x41 default 5000-droplet deterministic simulation\naverage_cpu_ms=" << milliseconds
                << "\nchanged_samples=" << changed << "\neroded=" << measuredBudget.eroded
                << "\ndeposited=" << measuredBudget.deposited << "\nexported=" << measuredBudget.exported << '\n';
            metrics.close();
            return checks.expect(static_cast<bool>(metrics), "write erosion measurements");
        }

        bool invalidInputs(Checks& checks)
        {
            Heightmap source, destination;
            source.createFlat(17, 9, 0.75f);
            destination.createFlat(3, 5, 0.25f);
            const auto previous = destination;
            TerrainErosionSettings settings;
            TerrainErosionBudget budget{ 1, 2, 3 };
            const auto rejected = [&](const TerrainErosionSettings& invalid)
            {
                return checks.expect(!validateTerrainErosionSettings(invalid) && !erodeTerrain(source, invalid, destination, nullptr, &budget)
                    && same(destination, previous) && budget.eroded == 1 && budget.deposited == 2 && budget.exported == 3,
                    "invalid erosion input preserves destination and budget");
            };
            for (auto member : { &TerrainErosionSettings::droplets, &TerrainErosionSettings::maxLifetime, &TerrainErosionSettings::radius })
                for (const auto value : { 0u, UINT32_MAX })
                {
                    auto invalid = settings;
                    invalid.*member = value;
                    if (!rejected(invalid))
                        return false;
                }
            for (auto member : { &TerrainErosionSettings::inertia, &TerrainErosionSettings::capacity, &TerrainErosionSettings::minSlope,
                &TerrainErosionSettings::depositRate, &TerrainErosionSettings::erosionRate, &TerrainErosionSettings::evaporation,
                &TerrainErosionSettings::gravity, &TerrainErosionSettings::initialWater, &TerrainErosionSettings::initialSpeed })
                for (const float value : { -1.0f, 101.0f, NAN, INFINITY, -INFINITY })
                {
                    auto invalid = settings;
                    invalid.*member = value;
                    if (!rejected(invalid))
                        return false;
                }
            for (auto member : { &TerrainErosionSettings::capacity, &TerrainErosionSettings::minSlope, &TerrainErosionSettings::initialWater })
            {
                auto invalid = settings;
                invalid.*member = 0;
                if (!rejected(invalid))
                    return false;
            }
            const std::array bounds{
                std::tuple{ &TerrainErosionSettings::inertia, 0.0f, 1.0f },
                std::tuple{ &TerrainErosionSettings::capacity, 0.01f, 100.0f },
                std::tuple{ &TerrainErosionSettings::minSlope, 0.00001f, 1.0f },
                std::tuple{ &TerrainErosionSettings::depositRate, 0.0f, 1.0f },
                std::tuple{ &TerrainErosionSettings::erosionRate, 0.0f, 1.0f },
                std::tuple{ &TerrainErosionSettings::evaporation, 0.0f, 1.0f },
                std::tuple{ &TerrainErosionSettings::gravity, 0.0f, 100.0f },
                std::tuple{ &TerrainErosionSettings::initialWater, 0.01f, 10.0f },
                std::tuple{ &TerrainErosionSettings::initialSpeed, 0.0f, 10.0f }
            };
            for (const auto& [member, minimum, maximum] : bounds)
            {
                for (const auto value : { minimum, maximum })
                {
                    auto valid = settings;
                    valid.*member = value;
                    if (!checks.expect(validateTerrainErosionSettings(valid), "inclusive floating recipe bounds"))
                        return false;
                }
                for (const auto value : { std::nextafter(minimum, -INFINITY), std::nextafter(maximum, INFINITY) })
                {
                    auto invalid = settings;
                    invalid.*member = value;
                    if (!rejected(invalid))
                        return false;
                }
            }
            for (const auto& [member, maximum] : std::array{
                std::pair{ &TerrainErosionSettings::droplets, 1000000u },
                std::pair{ &TerrainErosionSettings::maxLifetime, 256u },
                std::pair{ &TerrainErosionSettings::radius, 8u } })
            {
                auto valid = settings;
                valid.*member = maximum;
                if (!checks.expect(validateTerrainErosionSettings(valid), "inclusive integer recipe maximum"))
                    return false;
                valid.*member = maximum + 1;
                if (!rejected(valid))
                    return false;
            }
            std::atomic<bool> cancelled = true;
            if (!checks.expect(!erodeTerrain(source, settings, destination, &cancelled, &budget) && same(destination, previous)
                && budget.eroded == 1, "cancelled erosion is transactional"))
                return false;
            Heightmap owner = std::move(source);
            return checks.expect(!erodeTerrain(source, settings, destination) && same(destination, previous),
                "moved-from erosion source rejected safely");
        }

        bool persistence(Checks& checks, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "Erosion");
            auto* object = scene.createGameObject("Eroded terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            TerrainNoiseSettings noise;
            noise.width = 41;
            noise.height = 29;
            TerrainErosionSettings settings{ 987, 2000, 40, 2, 0.1f, 3, 0.002f, 0.4f, 0.2f, 0.02f, 5, 2, 0.5f };
            if (!checks.expect(terrain->generateNoise(noise, { 40, 28, 4, -1, 2, 3 })
                && terrain->setChunkSettings({ 16 }) && terrain->setLodSettings({ true, 3, 17 })
                && terrain->applyErosion(settings), "apply erosion recipe synchronously"))
                return false;
            const auto eroded = terrain->getHeightmap();
            std::vector<std::uint8_t> payload, rebuilt;
            TerrainComponent restored, clone;
            if (!checks.expect(terrain->serializePayload(payload) && terrain->getPayloadVersion() == 6
                && restored.deserializePayload(6, payload) && restored.getErosionSettings() == settings
                && same(restored.getHeightmap(), eroded) && terrain->copyConfigurationTo(clone)
                && clone.getErosionSettings() == settings && same(clone.getHeightmap(), eroded),
                "v6 load/clone retain authoritative eroded samples and complete recipe without replay"))
                return false;
            if (!checks.expect(terrain->setTerrain(eroded, terrain->getSettings()) && terrain->serializePayload(rebuilt)
                && rebuilt == payload, "ordinary rebuild does not repeat erosion"))
                return false;
            for (const auto version : { 1u, 2u, 3u, 4u, 5u })
                if (!checks.expect(restored.deserializePayload(version, payload) && same(restored.getHeightmap(), eroded)
                    && restored.getErosionSettings() == TerrainErosionSettings{}, "v1..5 migration defaults recipe without altering samples"))
                    return false;
            for (int invalid = 0; invalid < 3; ++invalid)
            {
                flatbuffers::FlatBufferBuilder builder;
                flatbuffers::Offset<Serialization::TerrainMaterialData> material;
                if (!Serialization::writeTerrainMaterial(builder, terrain->getMaterialSettings(), material))
                    return false;
                const auto recipe = Serialization::CreateTerrainNoiseSettings(builder);
                const auto lod = Serialization::CreateTerrainLodSettings(builder);
                const auto erosion = invalid == 0 ? flatbuffers::Offset<Serialization::TerrainErosionSettings>{}
                    : Serialization::CreateTerrainErosionSettings(builder, 0, invalid == 1 ? 0u : 20u, 40, 3,
                        invalid == 2 ? NAN : 0.05f);
                const auto samples = eroded.samples();
                const auto root = Serialization::CreateTerrainComponentPayload(builder, eroded.width(), eroded.height(),
                    builder.CreateVector(samples.data(), samples.size()), 40, 28, 4, -1, 2, 3, true, recipe, material, 16, lod, erosion);
                Serialization::FinishTerrainComponentPayloadBuffer(builder, root);
                const auto model = terrain->getModel();
                if (!checks.expect(!terrain->deserializePayload(6, { builder.GetBufferPointer(), builder.GetSize() })
                    && terrain->getModel() == model && terrain->getErosionSettings() == settings && same(terrain->getHeightmap(), eroded),
                    "missing/malformed v6 recipe rejects before modifying terrain"))
                    return false;
                if (invalid == 0)
                {
                    TerrainComponent legacy;
                    if (!checks.expect(legacy.deserializePayload(5, { builder.GetBufferPointer(), builder.GetSize() })
                        && same(legacy.getHeightmap(), eroded) && legacy.getErosionSettings() == TerrainErosionSettings{},
                        "genuine v5 payload without erosion table loads unchanged"))
                        return false;
                }
            }
            Scene loaded(ObjectGUID::generate(), "Loaded erosion"), cloned(ObjectGUID::generate(), "Play erosion");
            const SceneClonePair pair{ scene, cloned };
            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"erosion.scene", scene)
                && Serialization::SceneSerializer{}.load(directory / L"erosion.scene", loaded)
                && cloneScenes(std::span(&pair, 1)), "eroded Scene/Play roundtrip"))
                return false;
            for (const auto* copy : { &loaded, &cloned })
            {
                const auto* copyObject = copy->find(object->getGUID());
                const auto* copyTerrain = copyObject ? copyObject->getComponent<TerrainComponent>() : nullptr;
                if (!checks.expect(copyTerrain && copyTerrain->getErosionSettings() == settings
                    && same(copyTerrain->getHeightmap(), eroded) && copyTerrain->getModel() != terrain->getModel(),
                    "Scene/Play retains independent eroded geometry and recipe"))
                    return false;
            }
            Prefab prefab, loadedPrefab;
            Scene instances(ObjectGUID::generate(), "Erosion prefab");
            if (!checks.expect(prefab.capture(*object) && Serialization::PrefabSerializer{}.save(directory / L"erosion.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"erosion.prefab", loadedPrefab), "eroded Prefab roundtrip"))
                return false;
            const auto* instance = loadedPrefab.instantiate(instances);
            const auto* instanceTerrain = instance ? instance->getComponent<TerrainComponent>() : nullptr;
            return checks.expect(instanceTerrain && instanceTerrain->getErosionSettings() == settings
                && same(instanceTerrain->getHeightmap(), eroded), "Prefab instantiates eroded samples without applying recipe again");
        }

        bool physicsAndAsync(Checks& checks)
        {
            Scene scene(ObjectGUID::generate(), "Erosion physics");
            auto* object = scene.createGameObject("Terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            TerrainNoiseSettings noise;
            noise.width = 41;
            noise.height = 29;
            const TerrainMeshSettings mesh{ 40, 28, 2, -1, 1, 1 };
            TerrainErosionSettings settings;
            settings.droplets = 4000;
            TerrainComponent reference;
            TerrainMaterialSettings material;
            material.enabled = true;
            if (!checks.expect(terrain->generateNoise(noise, mesh) && terrain->setChunkSettings({ 16 })
                && terrain->setLodSettings({ true, 4, 1 }) && terrain->setMaterialSettings(material)
                && reference.generateNoise(noise, mesh) && reference.setChunkSettings({ 16 })
                && reference.setLodSettings({ true, 4, 1 }) && reference.setMaterialSettings(material)
                && reference.applyErosion(settings), "eroded PhysX/sync oracle fixture"))
                return false;
            scene.update(0);
            const auto oldModel = terrain->getModel();
            const auto oldCollision = collider->getPhysicsAsset();
            std::vector<std::uint8_t> before, pending, actual, expected;
            if (!checks.expect(terrain->serializePayload(before) && terrain->requestErosion(settings)
                && terrain->serializePayload(pending) && pending == before && terrain->getModel() == oldModel
                && collider->getPhysicsAsset() == oldCollision, "pending erosion preserves applied save/render/PhysX state")
                || !await(checks, *terrain, false))
                return false;
            TerrainComponent pendingClone;
            if (!checks.expect(terrain->copyConfigurationTo(pendingClone) && pendingClone.serializePayload(actual)
                && actual == before && pendingClone.getGenerationStatus().stage == TerrainGenerationStage::Idle,
                "clone during erosion uses applied samples/recipe, without copying pending work"))
                return false;
            auto invalid = settings;
            invalid.radius = 0;
            if (!checks.expect(!terrain->requestErosion(invalid) && !terrain->applyErosion(invalid)
                && terrain->getGenerationStatus().stage == TerrainGenerationStage::Ready, "invalid erosion does not supersede accepted work")
                || !await(checks, *terrain)
                || !checks.expect(terrain->serializePayload(actual) && reference.serializePayload(expected) && actual == expected,
                    "async and sync erosion payloads are byte-identical"))
                return false;
            auto* world = PhysicsSystem::instance().getWorld();
            const auto model = ModelManager::instance().get(terrain->getModel());
            const auto oracle = ModelManager::instance().get(reference.getModel());
            std::vector<std::shared_ptr<const PhysicsMeshAsset>> cooked;
            if (!checks.expect(model && oracle && model->meshes.size() == oracle->meshes.size()
                && collider->getChunkAssets().size() == terrain->getChunks().size() && cookTerrainChunks(*oracle, cooked),
                "erosion repartitions/cooks every chunk"))
                return false;
            for (std::size_t chunk = 0; chunk < model->meshes.size(); ++chunk)
            {
                const auto& a = model->meshes[chunk];
                const auto& b = oracle->meshes[chunk];
                if (!checks.expect(a.indices == b.indices && a.vertices.size() == b.vertices.size()
                    && std::ranges::equal(cooked[chunk]->getCookedData(), collider->getChunkAssets()[chunk]->getCookedData()),
                    "eroded sync/async LOD indices and full-resolution PhysX cooking match"))
                    return false;
                for (std::size_t vertex = 0; vertex < a.vertices.size(); ++vertex)
                {
                    const auto& p = a.vertices[vertex];
                    const auto& q = b.vertices[vertex];
                    if (!checks.expect(p.position == q.position && p.normal == q.normal && p.tangent == q.tangent
                        && p.bitangent == q.bitangent && p.texCoord == q.texCoord && p.color == q.color,
                        "every eroded vertex/normal/tangent/UV/layer weight matches synchronous oracle"))
                        return false;
                }
            }
            for (int z = -6; z <= 6; ++z)
                for (int x = -9; x <= 9; ++x)
                {
                    const Vector3 position(x * 2.0f + 0.13f, 10, z * 2.0f + 0.23f);
                    const auto surface = terrain->sampleWorldSurface(position);
                    const auto hit = world->raycast(position, -Vector3::UnitY, 20);
                    if (!checks.expect(surface && hit && hit->collider == collider && std::abs(surface->position.y - hit->point.y) < 0.001f
                        && surface->normal.Dot(hit->normal) > 0.999f, "eroded triangle query matches rebuilt PhysX height and normal"))
                        return false;
                }
            auto* player = scene.createGameObject("Character on erosion");
            player->getTransform()->setPosition(Vector3(0, 3, 0));
            auto* character = player->addComponent<CharacterControllerComponent>();
            character->setLayer(PhysicsLayer::Player);
            scene.update(0);
            for (int step = 0; step < 80; ++step)
            {
                character->move(Vector3(0, -0.1f, 0), 1.0f / 60);
                world->simulate(1.0f / 60);
                world->fetchResults();
            }
            const auto surface = terrain->sampleWorldSurface(character->getPosition());
            if (!checks.expect(surface && character->isGrounded() && std::abs(character->getPosition().y - surface->position.y) < 0.05f,
                "existing CharacterController grounds on eroded PhysX terrain"))
                return false;
            player->setActive(false);
            const auto applied = terrain->getModel();
            const auto asset = collider->getPhysicsAsset();
            auto failedRecipe = settings;
            failedRecipe.seed = 888;
            if (!checks.expect(terrain->requestErosion(failedRecipe), "request cumulative erosion") || !await(checks, *terrain, false))
                return false;
            world->simulate(1.0f / 60);
            TerrainGenerationQueue::instance().applyCompleted();
            world->fetchResults();
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Failed
                && terrain->getModel() == applied && collider->getPhysicsAsset() == asset
                && terrain->getErosionSettings() == settings && terrain->serializePayload(actual) && actual == expected,
                "busy PhysX rejects erosion atomically, retaining old samples/recipe/resources"))
                return false;
            if (!checks.expect(terrain->requestErosion(settings), "queue cancellable erosion") || !await(checks, *terrain, false))
                return false;
            terrain->cancelGeneration();
            TerrainGenerationQueue::instance().applyCompleted();
            if (!checks.expect(terrain->getModel() == applied && terrain->getGenerationStatus().stage == TerrainGenerationStage::Cancelled,
                "cancel Ready erosion discards result"))
                return false;
            settings.droplets = 1000000;
            if (!checks.expect(terrain->requestErosion(settings), "queue active erosion cancellation"))
                return false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (terrain->getGenerationStatus().stage != TerrainGenerationStage::Erosion && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!checks.expect(terrain->getGenerationStatus().stage == TerrainGenerationStage::Erosion, "observe actual off-thread erosion stage"))
                return false;
            terrain->cancelGeneration();
            settings.droplets = 2000;
            settings.seed = 89;
            if (!checks.expect(terrain->requestErosion(settings) && reference.applyErosion(settings), "latest erosion supersedes active cancelled work")
                || !await(checks, *terrain))
                return false;
            return checks.expect(terrain->serializePayload(actual) && reference.serializePayload(expected) && actual == expected,
                "latest accepted erosion is cumulative on applied input, not cancelled pending result");
        }
    }

    bool validateTerrainErosion(const std::filesystem::path& directory, std::size_t& passed)
    {
        passed = 0;
        Checks checks;
        if (!algorithm(checks, directory) || !invalidInputs(checks) || !persistence(checks, directory) || !physicsAndAsync(checks))
            return false;
        passed = checks.count;
        LOG_INFO("[TerrainErosionValidation] PASS: {} checks.", passed);
        return true;
    }
}
