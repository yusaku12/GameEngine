#include "Pch.h"
#include "Validation\Terrain\TerrainCollisionValidation.h"
#include "Terrain\Geometry\TerrainSurface.h"
#include "Components\Terrain\TerrainComponent.h"
#include "Core\Scene\Scene.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Assets\Model\ModelManager.h"
#include "Components\Physics\Collider\TerrainColliderComponent.h"
#include "Components\Physics\Collider\SphereColliderComponent.h"
#include "Components\Physics\Character\CharacterControllerComponent.h"
#include "Components\Physics\Rigidbody\RigidbodyComponent.h"
#include "Physics\PhysicsEventListener.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Core\Scene\SceneManager.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Generated\FlatBuffers\PhysicsComponent_generated.h"
#include "Physics\Internal\PhysicsPayload.h"
#include <cmath>
#include <limits>

namespace Engine
{
    namespace
    {
        struct Checks
        {
            std::size_t count = 0;
            bool expect(const bool condition, const char* label)
            {
                if (!condition)
                    LOG_ERROR("[TerrainCollisionValidation] FAILED: {}.", label);
                else
                    ++count;
                return condition;
            }
            bool close(const float actual, const float expected, const char* label, const float tolerance = 0.0002f)
            {
                if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
                    LOG_ERROR("[TerrainCollisionValidation] {}: actual={} expected={}.", label, actual, expected);
                return expect(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, label);
            }
        };

        bool surfaceQueries(Checks& checks)
        {
            Heightmap heights;
            TerrainMeshSettings settings{ 4, 2, 8, -3, 1, 1 };
            const std::array<float, 4> saddle{ 0, 0, 0, 1 };
            if (!checks.expect(heights.assign(2, 2, saddle), "saddle height fixture"))
                return false;
            for (const auto& [point, height] : std::array{
                std::pair{ Vector2(-2, -1), -3.0f }, std::pair{ Vector2(2, 1), 5.0f },
                std::pair{ Vector2(0, 0), -3.0f }, std::pair{ Vector2(1, 0.5f), 1.0f },
                std::pair{ Vector2(2, -1), -3.0f }, std::pair{ Vector2(-2, 1), -3.0f } })
            {
                const auto hit = sampleTerrainSurface(heights, settings, point.x, point.y);
                if (!checks.expect(hit.has_value(), "inclusive terrain sample boundaries")
                    || !checks.close(hit->position.y, height, "triangle-exact height not bilinear")
                    || !checks.close(hit->normal.Length(), 1, "unit geometric normal"))
                    return false;
            }
            for (const auto point : { Vector2(-2.01f, 0), Vector2(2.01f, 0), Vector2(0, -1.01f), Vector2(0, 1.01f) })
                if (!checks.expect(!sampleTerrainSurface(heights, settings, point.x, point.y), "outside terrain is a miss, not clamped"))
                    return false;
            if (!checks.expect(!sampleTerrainSurface(heights, settings, NAN, 0)
                && !sampleTerrainSurface(heights, settings, 0, INFINITY), "nonfinite sample rejected"))
                return false;
            TerrainNoiseSettings recipe;
            recipe.width = 14;
            recipe.height = 10;
            recipe.seed = 72;
            recipe.offsetX = -2.37f;
            recipe.offsetZ = 4.82f;
            settings = { 8.31f, 6.17f, 5, -1, 1, 1 };
            ModelResource model;
            if (!checks.expect(generateTerrainNoise(recipe, heights) && buildTerrainModel(heights, settings, model),
                "non-square query oracle mesh"))
                return false;
            const std::array transforms{
                Matrix::Identity,
                Matrix::CreateScale(2, 0.6f, 1.3f) * Matrix::CreateRotationY(0.7f) * Matrix::CreateTranslation(3, -4, 2),
                Matrix::CreateRotationX(0.6f) * Matrix::CreateRotationZ(0.4f),
                Matrix::CreateRotationZ(2.1f),
                Matrix::CreateScale(-1, 2, 1) * Matrix::CreateRotationY(-0.3f),
                Matrix(1, 0, 0, 0, 0.2f, 1, -0.3f, 0, 0.1f, 0, 1, 0, 0, 0, 0, 1),
            };
            for (const auto& world : transforms)
            {
                std::vector<Vector3> vertices;
                Vector3 minimum(FLT_MAX), maximum(-FLT_MAX);
                for (const auto& vertex : model.meshes[0].vertices)
                {
                    const auto position = Vector3::Transform(vertex.position, world);
                    vertices.push_back(position);
                    minimum = Vector3::Min(minimum, position);
                    maximum = Vector3::Max(maximum, position);
                }
                for (int row = 0; row <= 12; ++row)
                    for (int column = 0; column <= 16; ++column)
                    {
                        const float x = std::lerp(minimum.x - 0.1f, maximum.x + 0.1f, static_cast<float>(column) / 16);
                        const float z = std::lerp(minimum.z - 0.1f, maximum.z + 0.1f, static_cast<float>(row) / 12);
                        const Vector3 origin(x, 100, z);
                        float nearest = FLT_MAX;
                        std::vector<std::pair<float, Vector3>> oracleHits;
                        const auto& indices = model.meshes[0].indices;
                        for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3)
                        {
                            const auto a = vertices[indices[triangle]], b = vertices[indices[triangle + 1]],
                                c = vertices[indices[triangle + 2]];
                            float distance = 0;
                            if (DirectX::TriangleTests::Intersects(origin, -Vector3::UnitY, a, b, c, distance))
                            {
                                nearest = std::min(nearest, distance);
                                auto expectedNormal = (b - a).Cross(c - a);
                                expectedNormal.Normalize();
                                if (expectedNormal.y < 0)
                                    expectedNormal = -expectedNormal;
                                oracleHits.emplace_back(distance, expectedNormal);
                            }
                        }
                        const auto actual = sampleTerrainSurfaceWorld(heights, settings, world, Vector3(x, -100, z));
                        if (!checks.expect(actual.has_value() == (nearest != FLT_MAX), "world DDA agrees with independent all-triangle oracle"))
                            return false;
                        if (actual && (!checks.close(actual->position.y, 100 - nearest, "highest transformed world height", 0.001f)
                            || !checks.expect(std::ranges::any_of(oracleHits, [&](const auto& candidate)
                                {
                                    // Shared edges have one height but multiple valid geometric normals.
                                    return std::abs(candidate.first - nearest) < 0.0001f
                                        && actual->normal.Dot(candidate.second) > 0.999f;
                                }), "world geometric normal follows a triangle at the highest intersection")))
                            return false;
                    }
            }
            if (!checks.expect(!sampleTerrainSurfaceWorld(heights, settings, Matrix::CreateScale(1, 0, 1), Vector3::Zero)
                && !sampleTerrainSurfaceWorld(heights, settings, Matrix::Identity, Vector3(NAN, 0, 0)), "invalid world query rejected"))
                return false;
            settings = { 0.01f, 0.01f, 100000, 100000, 1, 1 };
            if (!checks.expect(heights.createFlat(1025, 1025, 1), "maximum-grid extreme query fixture"))
                return false;
            const auto localExtreme = sampleTerrainSurface(heights, settings, 0, 0);
            const auto worldExtreme = sampleTerrainSurfaceWorld(heights, settings, Matrix::Identity, Vector3::Zero);
            if (!checks.expect(localExtreme && worldExtreme, "maximum-grid queries require no mesh allocation")
                || !checks.close(localExtreme->position.y, 200000, "extreme local sample height")
                || !checks.close(worldExtreme->position.y, 200000, "extreme world sample height"))
                return false;
            return true;
        }

        void step(Scene& scene, PhysicsWorld& world)
        {
            scene.fixedUpdate(1.0f / 60);
            world.simulate(1.0f / 60);
            world.fetchResults();
            world.dispatchEvents();
        }

        bool persistence(Checks& checks, const std::filesystem::path& directory, PhysicsWorld& world)
        {
            const auto baseline = world.getStatistics().activeShapes;
            Scene source(ObjectGUID::generate(), "Terrain collision source");
            auto* object = source.createGameObject("Terrain");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            collider->setLayer(PhysicsLayer::Environment);
            collider->setCollisionMask(physicsLayerBit(PhysicsLayer::Player));
            PhysicsMaterial material;
            material.setStaticFriction(0.8f);
            material.setDynamicFriction(0.7f);
            material.setRestitution(0.1f);
            collider->setMaterial(material);
            collider->setCenter(Vector3(0.25f, 0.1f, -0.5f));
            collider->setEnabled(false);
            TerrainNoiseSettings recipe;
            recipe.width = 41;
            recipe.height = 36;
            if (!checks.expect(terrain->setChunkSettings({ 16 }) && terrain->generateNoise(recipe, { 8, 8, 3, 1, 1, 1 })
                && collider->getPhysicsAsset() && collider->getPhysicsAsset()->getPath().empty()
                && collider->getChunkAssets().size() == 9,
                "terrain cooks in-memory collision without requiring external physics file"))
                return false;
            Scene loaded(ObjectGUID::generate(), "Terrain collision loaded");
            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"collision.scene", source)
                && Serialization::SceneSerializer{}.load(directory / L"collision.scene", loaded), "terrain collider scene roundtrip"))
                return false;
            auto* restored = loaded.find(object->getGUID());
            auto* restoredCollider = restored ? restored->getComponent<TerrainColliderComponent>() : nullptr;
            if (!checks.expect(restoredCollider && !restoredCollider->isEnabled()
                && restoredCollider->getLayer() == PhysicsLayer::Environment
                && restoredCollider->getCollisionMask() == physicsLayerBit(PhysicsLayer::Player)
                && restoredCollider->getMaterial().getDynamicFriction() == 0.7f
                && restoredCollider->getCenter() == collider->getCenter()
                && world.getStatistics().activeShapes == baseline, "disabled loaded collider preserves settings without registering physics"))
                return false;
            Prefab prefab, loadedPrefab;
            Scene instances(ObjectGUID::generate(), "Terrain collider prefab");
            if (!checks.expect(prefab.capture(*object)
                && Serialization::PrefabSerializer{}.save(directory / L"collision.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"collision.prefab", loadedPrefab), "terrain collider prefab roundtrip"))
                return false;
            auto* instance = loadedPrefab.instantiate(instances);
            if (!checks.expect(instance && instance->getComponent<TerrainColliderComponent>()
                && !instance->getComponent<TerrainColliderComponent>()->isEnabled(), "prefab retains collider without cooked external dependency"))
                return false;
            Scene clone(ObjectGUID::generate(), "Terrain collision Play");
            const SceneClonePair pair{ source, clone };
            if (!checks.expect(cloneScenes(std::span(&pair, 1)) && world.getStatistics().activeShapes == baseline,
                "Play clone copies settings without adding physics actors before activation"))
                return false;
            collider->setEnabled(true);
            source.update(0);
            step(source, world);
            if (!checks.expect(world.getStatistics().activeShapes == baseline + 9, "source chunk collider activation"))
                return false;
            source.deactivateLifecycle();
            restoredCollider->setEnabled(true);
            loaded.update(0);
            step(loaded, world);
            const auto hit = world.raycast(Vector3(0, 20, 0), -Vector3::UnitY, 40);
            if (!checks.expect(hit && hit->collider == restoredCollider && restoredCollider->getChunkAssets().size() == 9
                && world.getStatistics().activeShapes == baseline + 9, "loaded collider regenerates all chunk shapes from embedded heights"))
                return false;
            std::vector<std::uint8_t> payload;
            if (!checks.expect(restoredCollider->serializePayload(payload), "serialize terrain collider common settings"))
                return false;
            const auto asset = restoredCollider->getPhysicsAsset();
            for (const auto size : { 0u, 4u, 7u })
                if (!checks.expect(!restoredCollider->deserializePayload(1, std::span(payload).first(size))
                    && restoredCollider->getPhysicsAsset() == asset, "truncated collider payload preserves shape asset"))
                    return false;
            flatbuffers::FlatBufferBuilder malformed;
            const Serialization::Vec3 center(0, 0, 0), size(0, 0, 0);
            const auto root = Serialization::CreateColliderComponentPayload(malformed, &center, 0.5f, 0.5f, 0, 0,
                ALL_PHYSICS_LAYERS, true, &size);
            PhysicsSerialization::writePayload(malformed, root, "CLCP", payload);
            if (!checks.expect(!restoredCollider->deserializePayload(1, payload)
                && !restoredCollider->isTrigger(), "terrain collider rejects serialized trigger"))
                return false;
            loaded.clear();
            if (!checks.expect(world.getStatistics().activeShapes == baseline, "loaded collider destruction removes all shapes"))
                return false;
            auto* clonedCollider = clone.find(object->getGUID())->getComponent<TerrainColliderComponent>();
            clonedCollider->setEnabled(true);
            clone.update(0);
            step(clone, world);
            if (!checks.expect(clonedCollider->getChunkAssets().size() == 9
                && world.getStatistics().activeShapes == baseline + 9
                && clonedCollider->getPhysicsAsset() != collider->getPhysicsAsset(),
                "Play activation creates independently cooked chunk shapes"))
                return false;
            clone.deactivateLifecycle();
            auto* instanceCollider = instance->getComponent<TerrainColliderComponent>();
            instanceCollider->setEnabled(true);
            instances.update(0);
            step(instances, world);
            if (!checks.expect(instanceCollider->getChunkAssets().size() == 9
                && world.getStatistics().activeShapes == baseline + 9, "prefab activation restores every chunk shape"))
                return false;
            instances.clear();
            return checks.expect(world.getStatistics().activeShapes == baseline, "clone and prefab chunk cleanup restores baseline");
        }

        class CollisionProbe final : public Component, public PhysicsEventListener
        {
        public:
            std::size_t enters = 0;
            std::size_t exits = 0;
            std::size_t triggerEnters = 0;
            std::size_t triggerExits = 0;
            void onCollisionEnter(const CollisionInfo&) override { ++enters; }
            void onCollisionExit(const CollisionInfo&) override { ++exits; }
            void onTriggerEnter(GameObject&) override { ++triggerEnters; }
            void onTriggerExit(GameObject&) override { ++triggerExits; }
        };

        bool restrictions(Checks& checks, PhysicsWorld& world)
        {
            const auto baseline = world.getStatistics().activeShapes;
            Scene scene(ObjectGUID::generate(), "Terrain restrictions");
            auto* object = scene.createGameObject("Source replacement");
            const auto* colliderType = ComponentRegistry::instance().get<TerrainColliderComponent>();
            const auto* bodyType = ComponentRegistry::instance().get<RigidbodyComponent>();
            if (!checks.expect(colliderType && bodyType
                && !colliderType->getEditorAdditionBlockReason(*object).empty(), "Inspector requires Terrain before Terrain Collider"))
                return false;
            auto* terrain = object->addComponent<TerrainComponent>();
            if (!checks.expect(colliderType->getEditorAdditionBlockReason(*object).empty(), "Inspector allows Terrain Collider on terrain"))
                return false;
            auto* collider = object->addComponent<TerrainColliderComponent>();
            collider->setIsTrigger(true);
            Heightmap elevated;
            if (!checks.expect(!collider->isTrigger() && !bodyType->getEditorAdditionBlockReason(*object).empty(),
                "terrain rejects triggers and Inspector Rigidbody addition")
                || !checks.expect(elevated.createFlat(2, 2, 1)
                    && terrain->setTerrain(elevated, { 10, 10, 5, 0, 1, 1 }), "prepare cooked source before Awake")
                || !checks.expect(object->removeComponent<TerrainComponent>(), "remove unawakened source with cached cooked data"))
                return false;
            object->addComponent<TerrainComponent>();
            scene.update(0);
            step(scene, world);
            auto hit = world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20);
            if (!checks.expect(hit && hit->collider == collider, "replacement before Awake activates new source")
                || !checks.close(hit->point.y, 0, "unawakened replacement never reuses stale cooked heights"))
                return false;
            collider->setCenter(Vector3(0, 0.25f, 0));
            hit = world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20);
            if (!checks.expect(hit.has_value(), "common collider center changes physics placement")
                || !checks.close(hit->point.y, 0.25f, "collider center is independent of terrain surface queries"))
                return false;
            collider->setEnabled(false);
            object->getTransform()->setScale(Vector3(-1, 1, 1));
            collider->setEnabled(true);
            if (!checks.expect(world.getStatistics().activeShapes == baseline, "negative terrain scale refuses PhysX registration"))
                return false;
            collider->setEnabled(false);
            object->getTransform()->setScale(Vector3(1, 0, 1));
            collider->setEnabled(true);
            if (!checks.expect(world.getStatistics().activeShapes == baseline, "zero terrain scale refuses PhysX registration"))
                return false;
            collider->setEnabled(false);
            object->getTransform()->setScale(Vector3::One);
            collider->setEnabled(true);
            if (!checks.expect(world.getStatistics().activeShapes == baseline + 1, "valid scale restores terrain collision"))
                return false;
            scene.clear();
            if (!checks.expect(world.getStatistics().activeShapes == baseline, "restriction fixtures clean up shapes"))
                return false;
            object = scene.createGameObject("Maximum chunk collision");
            terrain = object->addComponent<TerrainComponent>();
            Heightmap maximum;
            if (!checks.expect(terrain->setChunkSettings({ 16 }) && maximum.createFlat(1025, 1025, 0.25f)
                && terrain->setTerrain(maximum, { 1024, 1024, 4, 0, 1, 1 }), "maximum chunk collider fixture"))
                return false;
            collider = object->addComponent<TerrainColliderComponent>();
            scene.update(0);
            if (!checks.expect(collider->getChunkAssets().size() == 4096
                && world.getStatistics().activeShapes == baseline + 4096, "maximum 4096 PhysX chunk shapes activate"))
                return false;
            for (const Vector3 position : { Vector3(-511, 10, -511), Vector3(511, 10, 511),
                Vector3(0, 10, 0), Vector3(16, 10, 16) })
            {
                const auto ray = world.raycast(position, -Vector3::UnitY, 20);
                if (!checks.expect(ray && ray->collider == collider, "maximum chunk grid corners and seams remain queryable")
                    || !checks.close(ray->point.y, 1, "maximum chunk grid exact collision height"))
                    return false;
            }
            scene.clear();
            return checks.expect(world.getStatistics().activeShapes == baseline, "all 4096 chunk shapes clean up");
        }

        bool physics(Checks& checks, PhysicsWorld& world)
        {
            const auto baseline = world.getStatistics().activeShapes;
            auto& manager = SceneManager::instance();
            struct SceneScope
            {
                std::vector<std::unique_ptr<Scene>> scenes;
                Scene* active = nullptr;
                bool exchanged = false;
                ~SceneScope()
                {
                    if (exchanged && !SceneManager::instance().exchangeScenes(scenes, active))
                        LOG_ERROR("[TerrainCollisionValidation] Failed to restore managed scenes.");
                }
            } scope;
            scope.scenes.push_back(std::make_unique<Scene>(ObjectGUID::generate(), "Terrain PhysX"));
            scope.active = scope.scenes.front().get();
            scope.exchanged = manager.exchangeScenes(scope.scenes, scope.active);
            if (!checks.expect(scope.exchanged, "isolate managed scene for collision event dispatch"))
                return false;
            Scene& scene = *manager.getActiveScene();
            auto* object = scene.createGameObject("Ground");
            auto* terrain = object->addComponent<TerrainComponent>();
            auto* collider = object->addComponent<TerrainColliderComponent>();
            auto* probe = object->addComponent<CollisionProbe>();
            Heightmap heights;
            const TerrainMeshSettings settings{ 20, 20, 4, 0, 1, 1 };
            if (!checks.expect(terrain->setChunkSettings({ 16 }) && heights.createFlat(33, 33, 0.25f)
                && terrain->setTerrain(heights, settings), "flat chunked collider fixture"))
                return false;
            scene.update(0);
            step(scene, world);
            if (!checks.expect(terrain->getChunks().size() == 4 && collider->getChunkAssets().size() == 4
                && world.getStatistics().activeShapes == baseline + 4, "one active PhysX shape per terrain chunk"))
                return false;
            const auto chunkedModel = terrain->getModel();
            if (!checks.expect(terrain->setChunkSettings({ 1024 }) && world.getStatistics().activeShapes == baseline + 1,
                "repartition merges active PhysX shapes transactionally")
                || !checks.expect(terrain->setChunkSettings({ 16 }) && terrain->getModel() != chunkedModel
                    && world.getStatistics().activeShapes == baseline + 4, "repartition splits active PhysX shapes without stale shapes"))
                return false;
            auto hit = world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20);
            if (!checks.expect(hit && hit->gameObject == object->getGUID() && hit->collider == collider, "PhysX ray identifies terrain GameObject and Collider")
                || !checks.close(hit->point.y, 1, "PhysX triangles match height query")
                || !checks.expect(hit->normal.Dot(Vector3::UnitY) > 0.999f, "PhysX up-facing terrain normal"))
                return false;
            const auto cast = world.sphereCast(Vector3(0, 10, 0), 0.5f, -Vector3::UnitY, 20);
            if (!checks.expect(cast && cast->collider == collider, "sphere sweep collides with terrain"))
                return false;
            collider->setLayer(PhysicsLayer::Environment);
            PhysicsQueryFilter filter;
            filter.layerMask = physicsLayerBit(PhysicsLayer::Player);
            if (!checks.expect(!world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20, filter), "terrain query respects collision layer filter"))
                return false;
            filter.layerMask = physicsLayerBit(PhysicsLayer::Environment);
            if (!checks.expect(world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20, filter).has_value(), "matching terrain query layer hits"))
                return false;
            filter.ignoredGameObject = object;
            if (!checks.expect(!world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20, filter), "terrain query ignores selected object"))
                return false;
            collider->setLayer(PhysicsLayer::Enemy);
            filter.ignoredGameObject = nullptr;
            filter.layerMask = physicsLayerBit(PhysicsLayer::Enemy);
            if (!checks.expect(world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20, filter).has_value(),
                "query uses layer bit rather than layer index"))
                return false;
            collider->setLayer(PhysicsLayer::Environment);
            collider->setEnabled(false);
            if (!checks.expect(!world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20), "disabled terrain collider has no physics shape"))
                return false;
            auto altered = settings;
            altered.heightOffset = 2;
            if (!checks.expect(terrain->setTerrain(heights, altered) && !world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20),
                "disabled collider regeneration does not activate physics"))
                return false;
            collider->setEnabled(true);
            hit = world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20);
            if (!checks.expect(hit.has_value(), "re-enable uses latest geometry") || !checks.close(hit->point.y, 3, "updated collider height"))
                return false;
            const auto oldModel = terrain->getModel();
            const auto oldAsset = collider->getPhysicsAsset();
            world.simulate(1.0f / 60);
            const bool rejected = !terrain->setTerrain(heights, settings) && !terrain->setChunkSettings({ 32 });
            world.fetchResults();
            if (!checks.expect(rejected && terrain->getModel() == oldModel && collider->getPhysicsAsset() == oldAsset
                && terrain->getChunkSettings().cellsPerChunk == 16 && world.getStatistics().activeShapes == baseline + 4,
                "regeneration during pending PhysX step preserves render and collision resources"))
                return false;
            if (!checks.expect(terrain->setTerrain(heights, settings), "restore terrain after pending simulation")
                || !checks.close(world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20)->point.y, 1, "collision changes immediately after successful regeneration"))
                return false;
            object->getTransform()->setScale(Vector3(2, 1.5f, 0.5f));
            object->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(0.4f, 0.2f, 0.1f));
            object->getTransform()->setPosition(Vector3(3, 2, -4));
            step(scene, world);
            const auto expected = terrain->sampleWorldSurface(Vector3(3, 0, -4));
            hit = world.raycast(Vector3(3, 20, -4), -Vector3::UnitY, 40);
            if (!checks.expect(expected && hit, "transformed terrain supports queries and physics")
                || !checks.close(hit->point.y, expected->position.y, "transformed query and PhysX height agree", 0.001f)
                || !checks.expect(hit->normal.Dot(expected->normal) > 0.999f, "transformed collision and query normal agree"))
                return false;
            object->getTransform()->setScale(Vector3::One);
            object->getTransform()->setRotation(Quaternion::Identity);
            object->getTransform()->setPosition(Vector3::Zero);
            step(scene, world);
            auto* sphere = scene.createGameObject("Falling sphere");
            sphere->getTransform()->setPosition(Vector3(0, 5, 0));
            auto* sphereShape = sphere->addComponent<SphereColliderComponent>();
            sphereShape->setRadius(0.5f);
            sphere->addComponent<RigidbodyComponent>();
            scene.update(0);
            for (int frame = 0; frame < 180; ++frame)
                step(scene, world);
            if (!checks.close(sphere->getWorldPosition().y, 1.5f, "dynamic Rigidbody rests on terrain", 0.025f)
                || !checks.expect(probe->enters == 1 && probe->exits == 0,
                    "four chunk shapes produce one stable GameObject contact at the shared corner"))
                return false;
            sphere->setActive(false);
            world.dispatchEvents();
            if (!checks.expect(probe->exits == 1, "removing corner contact produces exactly one object-level exit"))
                return false;
            sphereShape->setIsTrigger(true);
            sphere->getComponent<RigidbodyComponent>()->setGravityEnabled(false);
            sphere->getTransform()->setPosition(Vector3(0, 1.25f, 0));
            sphere->setActive(true);
            scene.update(0);
            for (int frame = 0; frame < 3; ++frame)
                step(scene, world);
            if (!checks.expect(probe->triggerEnters == 1 && probe->triggerExits == 0,
                "trigger overlapping four chunk shapes dispatches one object-level enter"))
                return false;
            sphere->setActive(false);
            world.dispatchEvents();
            if (!checks.expect(probe->triggerExits == 1, "compound terrain trigger cleanup dispatches one exit"))
                return false;
            auto* player = scene.createGameObject("Character");
            player->getTransform()->setPosition(Vector3(-3, 4, 3));
            auto* character = player->addComponent<CharacterControllerComponent>();
            character->setLayer(PhysicsLayer::Player);
            scene.update(0);
            for (int frame = 0; frame < 100; ++frame)
            {
                character->move(Vector3(0, -0.1f, 0), 1.0f / 60);
                step(scene, world);
            }
            if (!checks.expect(character->isGrounded(), "Character Controller grounds on terrain")
                || !checks.close(character->getPosition().y, 1, "Character foot agrees with terrain height", 0.025f))
                return false;
            if (!checks.expect(heights.createFlat(33, 33, 0) && terrain->setTerrain(heights, settings), "lower terrain beneath cached Character Controller"))
                return false;
            for (int frame = 0; frame < 30; ++frame)
            {
                character->move(Vector3(0, -0.1f, 0), 1.0f / 60);
                step(scene, world);
            }
            if (!checks.expect(character->isGrounded(), "controller grounds on regenerated triangles")
                || !checks.close(character->getPosition().y, 0, "controller cached triangles invalidated on regeneration", 0.025f))
                return false;
            std::vector<float> ramp(33 * 33);
            for (std::size_t index = 0; index < ramp.size(); ++index)
                ramp[index] = static_cast<float>(index % 33) / 32;
            auto slopeSettings = settings;
            slopeSettings.heightOffset = -2;
            if (!checks.expect(heights.assign(33, 33, ramp) && terrain->setTerrain(heights, slopeSettings), "gentle walkable ramp fixture"))
                return false;
            PhysicsQueryFilter groundFilter;
            groundFilter.includeDynamic = false;
            for (int z = -4; z <= 4; ++z)
                for (int x = -4; x <= 4; ++x)
                {
                    const Vector3 position(x * 1.7f + 0.13f, 10, z * 1.7f + 0.23f);
                    const auto surface = terrain->sampleWorldSurface(position);
                    const auto ray = world.raycast(position, -Vector3::UnitY, 20, groundFilter);
                    if (!checks.expect(surface && ray && ray->collider == collider, "sloped terrain query and PhysX both hit")
                        || !checks.close(ray->point.y, surface->position.y, "sloped triangle height agrees with PhysX", 0.001f)
                        || !checks.expect(ray->normal.Dot(surface->normal) > 0.999f, "sloped triangle normal agrees with PhysX"))
                        return false;
                }
            for (int frame = 0; frame < 220; ++frame)
            {
                character->move(Vector3(0.03f, -0.1f, 0), 1.0f / 60);
                step(scene, world);
            }
            const auto ground = terrain->sampleWorldSurface(character->getPosition());
            if (!checks.expect(ground && character->isGrounded() && character->getPosition().x > 2.5f,
                "Character Controller walks across a chunk seam up terrain without height snapping")
                || !checks.close(character->getPosition().y, ground->position.y, "walking foot tracks triangle ramp", 0.06f))
                return false;
            collider->setCollisionMask(0);
            const float previousY = character->getPosition().y;
            for (int frame = 0; frame < 20; ++frame)
            {
                character->move(Vector3(0, -0.1f, 0), 1.0f / 60);
                step(scene, world);
            }
            if (!checks.expect(!character->isGrounded() && character->getPosition().y < previousY - 1,
                "Character Controller respects terrain's bilateral collision mask"))
                return false;
            sphere->setActive(false);
            player->setActive(false);
            if (!checks.expect(object->removeComponent<TerrainComponent>(), "remove terrain source"))
                return false;
            if (!checks.expect(!world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20), "removing Terrain removes stale Collider shape"))
                return false;
            terrain = object->addComponent<TerrainComponent>();
            if (!checks.expect(terrain && terrain->setTerrain(heights, settings), "replace removed terrain component"))
                return false;
            step(scene, world);
            if (!checks.expect(world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20).has_value(), "existing Terrain Collider reconnects to replacement Terrain"))
                return false;
            object->setActive(false);
            if (!checks.expect(!world.raycast(Vector3(0, 10, 0), -Vector3::UnitY, 20), "inactive terrain object removes collision"))
                return false;
            scene.clear();
            return checks.expect(world.getStatistics().activeShapes == baseline, "terrain/body/controller cleanup restores physics shape count");
        }
    }

    bool validateTerrainCollision(const std::filesystem::path& directory, std::size_t& passed)
    {
        passed = 0;
        Checks checks;
        auto* world = PhysicsSystem::instance().getWorld();
        if (!checks.expect(world && world->isInitialized(), "initialized PhysX terrain validation")
            || !surfaceQueries(checks) || !persistence(checks, directory, *world)
            || !restrictions(checks, *world) || !physics(checks, *world))
            return false;
        passed = checks.count;
        LOG_INFO("[TerrainCollisionValidation] PASS: {} surface and PhysX checks.", passed);
        return true;
    }
}
