#include "Pch.h"
#include "Core\GameObject\GameObject.h"
#include "Core\GameObject\Component\TransformComponent.h"
#include "Core\Scene\SceneManager.h"
#include "Physics\Character\CharacterControllerComponent.h"
#include "Physics\Collider\ColliderComponent.h"
#include "Physics\Collider\ConvexColliderComponent.h"
#include "Physics\Collider\MeshColliderComponent.h"
#include "Physics\Internal\PhysXConversions.h"
#include "Physics\Joint\JointComponent.h"
#include "Physics\PhysicsEventListener.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\Rigidbody\RigidbodyComponent.h"
#include "Graphics\Debug\DebugPrimitive.h"

#include <extensions/PxDefaultAllocator.h>
#include <extensions/PxDefaultCpuDispatcher.h>
#include <extensions/PxDefaultStreams.h>
#include <extensions/PxRigidBodyExt.h>
#include <cooking/PxCooking.h>
#include <characterkinematic/PxCapsuleController.h>
#include <characterkinematic/PxControllerManager.h>
#include <extensions/PxD6Joint.h>
#include <extensions/PxDistanceJoint.h>
#include <extensions/PxExtensionsAPI.h>
#include <extensions/PxFixedJoint.h>
#include <extensions/PxRevoluteJoint.h>
#include <extensions/PxShapeExt.h>
#include <atomic>
#include <mutex>

namespace Engine
{
    using namespace physx;

    namespace
    {
        constexpr float DEFAULT_GRAVITY = -9.81f;
        constexpr PxU32 DISPATCHER_THREAD_COUNT = 2;

        class PhysXErrorCallback final : public PxErrorCallback
        {
        public:

            void reportError(
                const PxErrorCode::Enum code,
                const char* message,
                const char* file,
                const int line) override
            {
                const char* const errorMessage = message != nullptr ? message : "Unknown PhysX error.";
                const char* const sourceFile = file != nullptr ? file : "<unknown>";
                if (code == PxErrorCode::eDEBUG_INFO)
                    LOG_INFO("[PhysX] {} ({}:{})", errorMessage, sourceFile, line);
                else if (code == PxErrorCode::eDEBUG_WARNING || code == PxErrorCode::ePERF_WARNING)
                    LOG_WARNING("[PhysX] {} ({}:{})", errorMessage, sourceFile, line);
                else
                    LOG_ERROR("[PhysX] {} ({}:{})", errorMessage, sourceFile, line);
            }
        };

        bool isFiniteVector(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool isPositiveVector(const Vector3& value) noexcept
        {
            return isFiniteVector(value) && value.x > 0.0f && value.y > 0.0f && value.z > 0.0f;
        }

        bool isValidGeometry(const PhysicsShapeGeometry& geometry) noexcept
        {
            if (!isFiniteVector(geometry.dimensions))
                return false;
            switch (geometry.type)
            {
            case PhysicsShapeType::Box:
                return isPositiveVector(geometry.dimensions);
            case PhysicsShapeType::Sphere:
            case PhysicsShapeType::Capsule:
                return geometry.dimensions.x > 0.0f && geometry.dimensions.y >= 0.0f;
            case PhysicsShapeType::TriangleMesh:
                return geometry.meshAsset != nullptr
                    && geometry.meshAsset->getType() == PhysicsMeshType::Triangle
                    && isPositiveVector(geometry.dimensions);
            case PhysicsShapeType::ConvexMesh:
                return geometry.meshAsset != nullptr
                    && geometry.meshAsset->getType() == PhysicsMeshType::Convex
                    && isPositiveVector(geometry.dimensions);
            default:
                return false;
            }
        }

        void setShapeTypeFlags(PxShape& shape, const bool isTrigger) noexcept
        {
            if (isTrigger)
            {
                shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
                shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
            }
            else
            {
                shape.setFlag(PxShapeFlag::eTRIGGER_SHAPE, false);
                shape.setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
            }
        }

        PxGeometryHolder toPxGeometry(
            const PhysicsShapeGeometry& geometry,
            PxPhysics& physics,
            PxTriangleMesh*& triangleMesh,
            PxConvexMesh*& convexMesh) noexcept
        {
            switch (geometry.type)
            {
            case PhysicsShapeType::Box:
                return PxGeometryHolder(PxBoxGeometry(
                    geometry.dimensions.x,
                    geometry.dimensions.y,
                    geometry.dimensions.z));
            case PhysicsShapeType::Sphere:
                return PxGeometryHolder(PxSphereGeometry(geometry.dimensions.x));
            case PhysicsShapeType::Capsule:
                return PxGeometryHolder(PxCapsuleGeometry(
                    geometry.dimensions.x,
                    geometry.dimensions.y));
            case PhysicsShapeType::TriangleMesh:
            {
                const std::span<const std::uint8_t> cookedData =
                    geometry.meshAsset->getCookedData();
                PxDefaultMemoryInputData input(cookedData.data(), cookedData.size());
                triangleMesh = physics.createTriangleMesh(input);
                if (triangleMesh == nullptr)
                    return PxGeometryHolder(PxBoxGeometry(0.5f, 0.5f, 0.5f));
                return PxGeometryHolder(PxTriangleMeshGeometry(
                    triangleMesh,
                    PxMeshScale(
                        PxVec3(geometry.dimensions.x, geometry.dimensions.y, geometry.dimensions.z),
                        PxQuat(PxIdentity))));
            }
            case PhysicsShapeType::ConvexMesh:
            {
                const std::span<const std::uint8_t> cookedData =
                    geometry.meshAsset->getCookedData();
                PxDefaultMemoryInputData input(cookedData.data(), cookedData.size());
                convexMesh = physics.createConvexMesh(input);
                if (convexMesh == nullptr)
                    return PxGeometryHolder(PxBoxGeometry(0.5f, 0.5f, 0.5f));
                return PxGeometryHolder(PxConvexMeshGeometry(
                    convexMesh,
                    PxMeshScale(
                        PxVec3(geometry.dimensions.x, geometry.dimensions.y, geometry.dimensions.z),
                        PxQuat(PxIdentity))));
            }
            default:
                return PxGeometryHolder(PxBoxGeometry(0.5f, 0.5f, 0.5f));
            }
        }

        PxTransform toShapePose(
            const ColliderComponent& component,
            const Vector3& scale,
            const PhysicsShapeType shapeType) noexcept
        {
            const Vector3& localCenter = component.getCenter();
            PxQuat localRotation(PxIdentity);
            if (shapeType == PhysicsShapeType::Capsule)
                localRotation = PxQuat(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));

            return PxTransform(
                PhysicsConversions::toPx(Vector3(
                    localCenter.x * std::abs(scale.x),
                    localCenter.y * std::abs(scale.y),
                    localCenter.z * std::abs(scale.z))),
                localRotation);
        }

        PxD6Axis::Enum toPxD6Axis(const PhysicsJointAxis axis) noexcept
        {
            return static_cast<PxD6Axis::Enum>(axis);
        }

        PxFilterFlags physicsSimulationFilterShader(
            const PxFilterObjectAttributes attributes0,
            const PxFilterData filterData0,
            const PxFilterObjectAttributes attributes1,
            const PxFilterData filterData1,
            PxPairFlags& pairFlags,
            const void* constantBlock,
            const PxU32 constantBlockSize)
        {
            GE_UNUSED(constantBlock);
            GE_UNUSED(constantBlockSize);

            if (filterData0.word0 >= 32 || filterData1.word0 >= 32
                || (filterData0.word1 & (1u << filterData1.word0)) == 0
                || (filterData1.word1 & (1u << filterData0.word0)) == 0)
                return PxFilterFlag::eSUPPRESS;

            if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
            {
                pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
            }
            else
            {
                pairFlags = PxPairFlag::eCONTACT_DEFAULT
                    | PxPairFlag::eNOTIFY_TOUCH_FOUND
                    | PxPairFlag::eNOTIFY_TOUCH_PERSISTS
                    | PxPairFlag::eNOTIFY_TOUCH_LOST
                    | PxPairFlag::eNOTIFY_CONTACT_POINTS;
            }
            return PxFilterFlag::eDEFAULT;
        }

        enum class PhysicsEventType
        {
            CollisionEnter,
            CollisionStay,
            CollisionExit,
            TriggerEnter,
            TriggerStay,
            TriggerExit
        };

        struct QueuedPhysicsEvent
        {
            ObjectGUID receiver;
            ObjectGUID other;
            PhysicsEventType type = PhysicsEventType::CollisionEnter;
            Vector3 point = Vector3::Zero;
            Vector3 normal = Vector3::Zero;
            float impulse = 0.0f;
        };

        struct ObjectPairKey
        {
            ObjectGUID first;
            ObjectGUID second;

            friend bool operator==(const ObjectPairKey&, const ObjectPairKey&) = default;
        };

        struct ObjectPairKeyHash
        {
            std::size_t operator()(const ObjectPairKey& pair) const noexcept
            {
                const std::size_t firstHash = ObjectGUIDHash{}(pair.first);
                const std::size_t secondHash = ObjectGUIDHash{}(pair.second);
                return firstHash ^ (secondHash + 0x9e3779b9u
                    + (firstHash << 6) + (firstHash >> 2));
            }
        };

        struct ColliderPairKey
        {
            const ColliderComponent* first = nullptr;
            const ColliderComponent* second = nullptr;

            friend bool operator==(const ColliderPairKey&, const ColliderPairKey&) = default;
        };

        struct ColliderPairKeyHash
        {
            std::size_t operator()(const ColliderPairKey& pair) const noexcept
            {
                const std::size_t firstHash = std::hash<const ColliderComponent*>{}(pair.first);
                const std::size_t secondHash = std::hash<const ColliderComponent*>{}(pair.second);
                return firstHash ^ (secondHash + 0x9e3779b9u
                    + (firstHash << 6) + (firstHash >> 2));
            }
        };

        ObjectPairKey makeObjectPairKey(const ObjectGUID& first, const ObjectGUID& second) noexcept
        {
            const bool firstIsLower = first.high < second.high
                || (first.high == second.high && first.low < second.low);
            return firstIsLower ? ObjectPairKey{ first, second } : ObjectPairKey{ second, first };
        }

        ColliderPairKey makeColliderPairKey(
            const ColliderComponent* const first,
            const ColliderComponent* const second) noexcept
        {
            return std::less<const ColliderComponent*>{}(first, second)
                ? ColliderPairKey{ first, second }
                : ColliderPairKey{ second, first };
        }

        class PhysicsQueryFilterCallback final : public PxQueryFilterCallback
        {
        public:

            explicit PhysicsQueryFilterCallback(const PhysicsQueryFilter& filter) noexcept
                : m_filter(filter)
            {
            }

            PxQueryHitType::Enum preFilter(
                const PxFilterData& filterData,
                const PxShape* shape,
                const PxRigidActor* actor,
                PxHitFlags& queryFlags) override
            {
                GE_UNUSED(filterData);
                GE_UNUSED(queryFlags);
                if (shape == nullptr || shape->userData == nullptr)
                    return PxQueryHitType::eNONE;

                const auto* const collider = static_cast<const ColliderComponent*>(shape->userData);
                if (collider->getGameObject() == nullptr
                    || collider->getGameObject() == m_filter.ignoredGameObject
                    || (filterData.word0 & physicsLayerBit(collider->getLayer())) == 0)
                    return PxQueryHitType::eNONE;

                if (!m_filter.includeTriggers && shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE))
                    return PxQueryHitType::eNONE;

                if (actor == nullptr)
                    return PxQueryHitType::eNONE;

                const PxActorType::Enum actorType = actor->getType();
                if (actorType == PxActorType::eRIGID_STATIC && !m_filter.includeStatic)
                    return PxQueryHitType::eNONE;
                if (actorType == PxActorType::eRIGID_DYNAMIC && !m_filter.includeDynamic)
                    return PxQueryHitType::eNONE;
                if (actorType != PxActorType::eRIGID_STATIC
                    && actorType != PxActorType::eRIGID_DYNAMIC)
                    return PxQueryHitType::eNONE;

                return PxQueryHitType::eBLOCK;
            }

            PxQueryHitType::Enum postFilter(
                const PxFilterData& filterData,
                const PxQueryHit& hit,
                const PxShape* shape,
                const PxRigidActor* actor) override
            {
                GE_UNUSED(filterData);
                GE_UNUSED(hit);
                GE_UNUSED(shape);
                GE_UNUSED(actor);
                return PxQueryHitType::eNONE;
            }

        private:

            const PhysicsQueryFilter& m_filter;
        };

        class CharacterShapeFilterCallback final : public PxQueryFilterCallback
        {
        public:

            CharacterShapeFilterCallback(
                const PhysicsLayer layer,
                const PhysicsLayerMask mask) noexcept
                : m_layer(layer)
                , m_mask(mask)
            {
            }

            PxQueryHitType::Enum preFilter(
                const PxFilterData& filterData,
                const PxShape* shape,
                const PxRigidActor* actor,
                PxHitFlags& queryFlags) override
            {
                GE_UNUSED(actor);
                GE_UNUSED(queryFlags);
                if (shape == nullptr
                    || shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE)
                    || filterData.word0 >= 32)
                    return PxQueryHitType::eNONE;

                const PxU32 ownLayer = static_cast<PxU32>(m_layer);
                return (m_mask & (1u << filterData.word0)) != 0
                    && (filterData.word1 & (1u << ownLayer)) != 0
                    ? PxQueryHitType::eBLOCK
                    : PxQueryHitType::eNONE;
            }

            PxQueryHitType::Enum postFilter(
                const PxFilterData& filterData,
                const PxQueryHit& hit,
                const PxShape* shape,
                const PxRigidActor* actor) override
            {
                GE_UNUSED(filterData);
                GE_UNUSED(hit);
                GE_UNUSED(shape);
                GE_UNUSED(actor);
                return PxQueryHitType::eNONE;
            }

        private:

            PhysicsLayer m_layer;
            PhysicsLayerMask m_mask;
        };

        class CharacterControllerFilterCallback final : public PxControllerFilterCallback
        {
        public:

            bool filter(const PxController& first, const PxController& second) override
            {
                const auto* const firstComponent =
                    static_cast<const CharacterControllerComponent*>(first.getUserData());
                const auto* const secondComponent =
                    static_cast<const CharacterControllerComponent*>(second.getUserData());
                if (firstComponent == nullptr || secondComponent == nullptr)
                    return false;

                return (firstComponent->getCollisionMask()
                    & physicsLayerBit(secondComponent->getLayer())) != 0
                    && (secondComponent->getCollisionMask()
                    & physicsLayerBit(firstComponent->getLayer())) != 0;
            }
        };

        PxQueryFilterData makeQueryFilterData(
            const PhysicsQueryFilter& filter,
            const bool reportAllHits = false) noexcept
        {
            PxQueryFlags flags = PxQueryFlag::ePREFILTER;
            if (filter.includeStatic)
                flags |= PxQueryFlag::eSTATIC;
            if (filter.includeDynamic)
                flags |= PxQueryFlag::eDYNAMIC;
            if (reportAllHits)
                flags |= PxQueryFlag::eNO_BLOCK;
            PxFilterData queryFilterData;
            queryFilterData.word0 = filter.layerMask;
            return PxQueryFilterData(queryFilterData, flags);
        }

        bool normalizeQueryDirection(const Vector3& direction, PxVec3& normalized) noexcept
        {
            if (!isFiniteVector(direction))
                return false;

            normalized = PhysicsConversions::toPx(direction);
            const PxReal lengthSquared = normalized.magnitudeSquared();
            if (!PxIsFinite(lengthSquared) || lengthSquared <= PX_EPS_F32)
                return false;

            normalized *= PxRecipSqrt(lengthSquared);
            return true;
        }

        bool isValidQueryDistance(const float distance) noexcept
        {
            return std::isfinite(distance) && distance >= 0.0f;
        }

        bool isValidQueryFilter(const PhysicsQueryFilter& filter) noexcept
        {
            return filter.includeStatic || filter.includeDynamic;
        }

        template <typename HitType>
        bool convertQueryHit(const HitType& source, RaycastHit& destination) noexcept
        {
            if (source.shape == nullptr || source.shape->userData == nullptr)
                return false;

            auto* const collider = static_cast<ColliderComponent*>(source.shape->userData);
            GameObject* const gameObject = collider->getGameObject();
            if (gameObject == nullptr)
                return false;

            destination.gameObject = gameObject->getGUID();
            destination.point = PhysicsConversions::fromPx(source.position);
            destination.normal = PhysicsConversions::fromPx(source.normal);
            destination.distance = source.distance;
            destination.collider = collider;
            return true;
        }
    } // namespace

    struct PhysicsWorld::Impl final : PxSimulationEventCallback
    {
        struct RigidbodyRecord
        {
            RigidbodyComponent* component = nullptr;
            PxRigidDynamic* actor = nullptr;
            PxTransform initialPose = PxTransform(PxIdentity);
        };

        struct ColliderRecord
        {
            ColliderComponent* component = nullptr;
            PxShape* shape = nullptr;
            PxRigidActor* actor = nullptr;
            float staticFriction = 0.0f;
            float dynamicFriction = 0.0f;
            float restitution = 0.0f;
            Vector3 lastWorldScale = Vector3::One;
        };

        struct ControllerRecord
        {
            CharacterControllerComponent* component = nullptr;
            PxCapsuleController* controller = nullptr;
            Vector3 initialPosition = Vector3::Zero;
            Quaternion initialLocalRotation = Quaternion::Identity;
        };

        struct JointRecord
        {
            JointComponent* component = nullptr;
            RigidbodyComponent* bodyA = nullptr;
            RigidbodyComponent* bodyB = nullptr;
            PxJoint* joint = nullptr;
        };

        struct ObjectPairState
        {
            PxU32 shapePairCount = 0;
            bool enteredThisStep = true;
            bool stayQueuedThisStep = false;
        };

        struct DebugContact
        {
            Vector3 point = Vector3::Zero;
            Vector3 normal = Vector3::Zero;
        };

        struct DebugRay
        {
            Vector3 start = Vector3::Zero;
            Vector3 end = Vector3::Zero;
        };

        PxDefaultAllocator allocator;
        PhysXErrorCallback errorCallback;
        PxFoundation* foundation = nullptr;
        PxPhysics* physics = nullptr;
        bool extensionsInitialized = false;
        PxDefaultCpuDispatcher* dispatcher = nullptr;
        PxScene* scene = nullptr;
        PxControllerManager* controllerManager = nullptr;
        Vector3 gravity = Vector3(0.0f, DEFAULT_GRAVITY, 0.0f);
        std::unordered_map<GameObject*, RigidbodyRecord> rigidbodies;
        std::unordered_map<GameObject*, PxRigidStatic*> staticActors;
        std::unordered_map<ColliderComponent*, ColliderRecord> colliders;
        std::unordered_map<CharacterControllerComponent*, ControllerRecord> controllers;
        std::unordered_map<JointComponent*, JointRecord> joints;
        std::mutex eventMutex;
        std::vector<QueuedPhysicsEvent> pendingEvents;
        std::unordered_map<ColliderPairKey, ObjectPairKey, ColliderPairKeyHash> activeContactShapePairs;
        std::unordered_map<ObjectPairKey, ObjectPairState, ObjectPairKeyHash> activeContactPairs;
        std::unordered_map<ColliderPairKey, ObjectPairKey, ColliderPairKeyHash> activeTriggerShapePairs;
        std::unordered_map<ObjectPairKey, ObjectPairState, ObjectPairKeyHash> activeTriggerPairs;
        mutable std::mutex debugMutex;
        std::vector<DebugContact> debugContacts;
        mutable std::vector<DebugRay> debugRays;
        std::atomic_bool captureDebugContacts = false;
        std::atomic_bool captureDebugRays = false;
        bool simulationPending = false;

        PxRigidDynamic* findRigidbodyActor(const RigidbodyComponent* const component) const noexcept
        {
            if (component == nullptr)
                return nullptr;
            for (const auto& [gameObject, record] : rigidbodies)
            {
                GE_UNUSED(gameObject);
                if (record.component == component)
                    return record.actor;
            }
            return nullptr;
        }

        bool reservePairEvents() noexcept
        {
            try
            {
                pendingEvents.reserve(pendingEvents.size() + 2);
                return true;
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("[PhysicsWorld] Failed to allocate space for physics events.");
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("[PhysicsWorld] Physics event queue reached its maximum capacity.");
            }
            return false;
        }

        bool enqueuePairEvent(
            const ObjectGUID& first,
            const ObjectGUID& second,
            const PhysicsEventType type,
            const Vector3& point = Vector3::Zero,
            const Vector3& normal = Vector3::Zero,
            const float impulse = 0.0f) noexcept
        {
            if (!reservePairEvents())
                return false;

            pendingEvents.push_back({ first, second, type, point, normal, impulse });
            pendingEvents.push_back({
                second,
                first,
                type,
                point,
                Vector3(-normal.x, -normal.y, -normal.z),
                impulse
            });
            return true;
        }

        void addContactPair(
            ColliderComponent& firstCollider,
            ColliderComponent& secondCollider,
            const Vector3& point,
            const Vector3& normal,
            const float impulse) noexcept
        {
            GameObject* const firstObject = firstCollider.getGameObject();
            GameObject* const secondObject = secondCollider.getGameObject();
            if (firstObject == nullptr || secondObject == nullptr || firstObject == secondObject)
                return;

            const ColliderPairKey shapePair = makeColliderPairKey(&firstCollider, &secondCollider);
            if (activeContactShapePairs.contains(shapePair))
                return;

            const ObjectPairKey objectPair = makeObjectPairKey(
                firstObject->getGUID(), secondObject->getGUID());
            auto objectPairIt = activeContactPairs.find(objectPair);
            bool newObjectPair = objectPairIt == activeContactPairs.end();
            if (newObjectPair)
            {
                try
                {
                    objectPairIt = activeContactPairs.emplace(
                        objectPair, ObjectPairState{}).first;
                }
                catch (const std::bad_alloc&)
                {
                    LOG_ERROR("[PhysicsWorld] Failed to track an active collision pair.");
                    return;
                }
                catch (const std::length_error&)
                {
                    LOG_ERROR("[PhysicsWorld] Active collision pair collection reached its maximum capacity.");
                    return;
                }
            }

            try
            {
                activeContactShapePairs.emplace(shapePair, objectPair);
            }
            catch (const std::bad_alloc&)
            {
                if (newObjectPair)
                    activeContactPairs.erase(objectPairIt);
                LOG_ERROR("[PhysicsWorld] Failed to track an active collision shape pair.");
                return;
            }
            catch (const std::length_error&)
            {
                if (newObjectPair)
                    activeContactPairs.erase(objectPairIt);
                LOG_ERROR("[PhysicsWorld] Active collision shape-pair collection reached its maximum capacity.");
                return;
            }

            if (newObjectPair)
            {
                if (!reservePairEvents())
                {
                    activeContactShapePairs.erase(shapePair);
                    activeContactPairs.erase(objectPairIt);
                    return;
                }
                pendingEvents.push_back({
                    firstObject->getGUID(),
                    secondObject->getGUID(),
                    PhysicsEventType::CollisionEnter,
                    point,
                    normal,
                    impulse
                });
                pendingEvents.push_back({
                    secondObject->getGUID(),
                    firstObject->getGUID(),
                    PhysicsEventType::CollisionEnter,
                    point,
                    Vector3(-normal.x, -normal.y, -normal.z),
                    impulse
                });
            }

            if (objectPairIt->second.shapePairCount != (std::numeric_limits<PxU32>::max)())
                ++objectPairIt->second.shapePairCount;
            else
                LOG_ERROR("[PhysicsWorld] Collision shape-pair count overflowed.");
        }

        void addTriggerPair(ColliderComponent& triggerCollider, ColliderComponent& otherCollider) noexcept
        {
            GameObject* const triggerObject = triggerCollider.getGameObject();
            GameObject* const otherObject = otherCollider.getGameObject();
            if (triggerObject == nullptr || otherObject == nullptr || triggerObject == otherObject)
                return;

            const ColliderPairKey shapePair = makeColliderPairKey(&triggerCollider, &otherCollider);
            if (activeTriggerShapePairs.contains(shapePair))
                return;

            const ObjectPairKey objectPair = makeObjectPairKey(
                triggerObject->getGUID(), otherObject->getGUID());
            auto objectPairIt = activeTriggerPairs.find(objectPair);
            const bool newObjectPair = objectPairIt == activeTriggerPairs.end();
            if (newObjectPair)
            {
                try
                {
                    objectPairIt = activeTriggerPairs.emplace(
                        objectPair, ObjectPairState{}).first;
                }
                catch (const std::bad_alloc&)
                {
                    LOG_ERROR("[PhysicsWorld] Failed to track an active trigger pair.");
                    return;
                }
                catch (const std::length_error&)
                {
                    LOG_ERROR("[PhysicsWorld] Active trigger pair collection reached its maximum capacity.");
                    return;
                }
            }

            try
            {
                activeTriggerShapePairs.emplace(shapePair, objectPair);
            }
            catch (const std::bad_alloc&)
            {
                if (newObjectPair)
                    activeTriggerPairs.erase(objectPairIt);
                LOG_ERROR("[PhysicsWorld] Failed to track an active trigger shape pair.");
                return;
            }
            catch (const std::length_error&)
            {
                if (newObjectPair)
                    activeTriggerPairs.erase(objectPairIt);
                LOG_ERROR("[PhysicsWorld] Active trigger shape-pair collection reached its maximum capacity.");
                return;
            }

            if (newObjectPair)
            {
                if (!reservePairEvents())
                {
                    activeTriggerShapePairs.erase(shapePair);
                    activeTriggerPairs.erase(objectPairIt);
                    return;
                }
                pendingEvents.push_back({
                    objectPair.first, objectPair.second, PhysicsEventType::TriggerEnter
                });
                pendingEvents.push_back({
                    objectPair.second, objectPair.first, PhysicsEventType::TriggerEnter
                });
            }

            if (objectPairIt->second.shapePairCount != (std::numeric_limits<PxU32>::max)())
            {
                ++objectPairIt->second.shapePairCount;
            }
            else
            {
                LOG_ERROR("[PhysicsWorld] Trigger shape-pair count overflowed.");
            }
        }

        void removeShapePair(
            const ColliderPairKey& shapePair,
            auto& activeShapePairs,
            auto& activeObjectPairs,
            const PhysicsEventType exitType) noexcept
        {
            const auto shapePairIt = activeShapePairs.find(shapePair);
            if (shapePairIt == activeShapePairs.end())
                return;

            const ObjectPairKey objectPair = shapePairIt->second;
            activeShapePairs.erase(shapePairIt);
            const auto objectPairIt = activeObjectPairs.find(objectPair);
            if (objectPairIt == activeObjectPairs.end())
            {
                LOG_ERROR("[PhysicsWorld] Active physics shape pair has no matching object pair.");
                return;
            }
            if (objectPairIt->second.shapePairCount > 1)
            {
                --objectPairIt->second.shapePairCount;
                return;
            }

            enqueuePairEvent(objectPair.first, objectPair.second, exitType);
            activeObjectPairs.erase(objectPairIt);
        }

        void removeActivePairsForCollider(ColliderComponent& component) noexcept
        {
            const std::lock_guard lock(eventMutex);
            for (auto it = activeContactShapePairs.begin(); it != activeContactShapePairs.end();)
            {
                if (it->first.first == &component || it->first.second == &component)
                {
                    const ColliderPairKey shapePair = it->first;
                    ++it;
                    removeShapePair(
                        shapePair,
                        activeContactShapePairs,
                        activeContactPairs,
                        PhysicsEventType::CollisionExit);
                }
                else
                {
                    ++it;
                }
            }
            for (auto it = activeTriggerShapePairs.begin(); it != activeTriggerShapePairs.end();)
            {
                if (it->first.first == &component || it->first.second == &component)
                {
                    const ColliderPairKey shapePair = it->first;
                    ++it;
                    removeShapePair(
                        shapePair,
                        activeTriggerShapePairs,
                        activeTriggerPairs,
                        PhysicsEventType::TriggerExit);
                }
                else
                {
                    ++it;
                }
            }
        }

        void removeTriggerPair(ColliderComponent& triggerCollider, ColliderComponent& otherCollider) noexcept
        {
            removeShapePair(
                makeColliderPairKey(&triggerCollider, &otherCollider),
                activeTriggerShapePairs,
                activeTriggerPairs,
                PhysicsEventType::TriggerExit);
        }

        void removeContactPair(ColliderComponent& firstCollider, ColliderComponent& secondCollider) noexcept
        {
            removeShapePair(
                makeColliderPairKey(&firstCollider, &secondCollider),
                activeContactShapePairs,
                activeContactPairs,
                PhysicsEventType::CollisionExit);
        }

        void queueContactStay(
            ColliderComponent& firstCollider,
            ColliderComponent& secondCollider,
            const Vector3& point,
            const Vector3& normal,
            const float impulse) noexcept
        {
            GameObject* const firstObject = firstCollider.getGameObject();
            GameObject* const secondObject = secondCollider.getGameObject();
            if (firstObject == nullptr || secondObject == nullptr)
                return;

            const ObjectPairKey key = makeObjectPairKey(
                firstObject->getGUID(), secondObject->getGUID());
            const auto found = activeContactPairs.find(key);
            if (found == activeContactPairs.end() || found->second.stayQueuedThisStep)
                return;

            if (enqueuePairEvent(
                firstObject->getGUID(),
                secondObject->getGUID(),
                PhysicsEventType::CollisionStay,
                point,
                normal,
                impulse))
            {
                found->second.stayQueuedThisStep = true;
            }
        }

        void appendTriggerStayEvents() noexcept
        {
            for (auto& [pair, state] : activeTriggerPairs)
            {
                if (state.enteredThisStep)
                {
                    state.enteredThisStep = false;
                    continue;
                }
                enqueuePairEvent(pair.first, pair.second, PhysicsEventType::TriggerStay);
            }
        }

        void resetContactStayEvents() noexcept
        {
            for (auto& [pair, state] : activeContactPairs)
            {
                GE_UNUSED(pair);
                state.stayQueuedThisStep = false;
            }
        }

        void onConstraintBreak(PxConstraintInfo* constraints, const PxU32 count) override
        {
            GE_UNUSED(constraints);
            GE_UNUSED(count);
        }

        void onWake(PxActor** actors, const PxU32 count) override
        {
            GE_UNUSED(actors);
            GE_UNUSED(count);
        }

        void onSleep(PxActor** actors, const PxU32 count) override
        {
            GE_UNUSED(actors);
            GE_UNUSED(count);
        }

        void onContact(
            const PxContactPairHeader& pairHeader,
            const PxContactPair* pairs,
            const PxU32 pairCount) override
        {
            GE_UNUSED(pairHeader);
            if (pairs == nullptr)
                return;

            const std::lock_guard lock(eventMutex);
            for (PxU32 pairIndex = 0; pairIndex < pairCount; ++pairIndex)
            {
                const PxContactPair& pair = pairs[pairIndex];
                if (pair.shapes[0] == nullptr || pair.shapes[1] == nullptr
                    || pair.flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_0)
                    || pair.flags.isSet(PxContactPairFlag::eREMOVED_SHAPE_1))
                    continue;

                auto* const firstCollider = static_cast<ColliderComponent*>(pair.shapes[0]->userData);
                auto* const secondCollider = static_cast<ColliderComponent*>(pair.shapes[1]->userData);
                if (firstCollider == nullptr || secondCollider == nullptr)
                    continue;

                if (pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_FOUND))
                {
                    Vector3 point = Vector3::Zero;
                    Vector3 normal = Vector3::Zero;
                    float impulse = 0.0f;
                    if (pair.contactCount > 0)
                    {
                        PxContactPairPoint contactPoint;
                        if (pair.extractContacts(&contactPoint, 1) > 0)
                        {
                            point = PhysicsConversions::fromPx(contactPoint.position);
                            normal = PhysicsConversions::fromPx(contactPoint.normal);
                            impulse = contactPoint.impulse.magnitude();
                        }
                    }

                    if (pair.contactCount > 0 && captureDebugContacts.load(std::memory_order_relaxed))
                    {
                        const std::lock_guard debugLock(debugMutex);
                        if (debugContacts.size() < 256)
                            debugContacts.push_back({ point, normal });
                    }
                    addContactPair(
                        *firstCollider, *secondCollider, point, normal, impulse);
                }
                else if (pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_PERSISTS))
                {
                    Vector3 point = Vector3::Zero;
                    Vector3 normal = Vector3::Zero;
                    float impulse = 0.0f;
                    if (pair.contactCount > 0)
                    {
                        PxContactPairPoint contactPoint;
                        if (pair.extractContacts(&contactPoint, 1) > 0)
                        {
                            point = PhysicsConversions::fromPx(contactPoint.position);
                            normal = PhysicsConversions::fromPx(contactPoint.normal);
                            impulse = contactPoint.impulse.magnitude();
                        }
                    }
                    if (pair.contactCount > 0 && captureDebugContacts.load(std::memory_order_relaxed))
                    {
                        const std::lock_guard debugLock(debugMutex);
                        if (debugContacts.size() < 256)
                            debugContacts.push_back({ point, normal });
                    }
                    queueContactStay(*firstCollider, *secondCollider, point, normal, impulse);
                }
                else if (pair.events.isSet(PxPairFlag::eNOTIFY_TOUCH_LOST))
                {
                    removeContactPair(*firstCollider, *secondCollider);
                }
            }
        }

        void recordDebugRay(const Vector3& start, const PxVec3& direction, const float distance) const
        {
            if (!captureDebugRays.load(std::memory_order_relaxed))
                return;
            const std::lock_guard lock(debugMutex);
            if (debugRays.size() < 128)
                debugRays.push_back({ start, start + PhysicsConversions::fromPx(direction) * distance });
        }

        void onTrigger(PxTriggerPair* pairs, const PxU32 pairCount) override
        {
            if (pairs == nullptr)
                return;

            const std::lock_guard lock(eventMutex);
            for (PxU32 pairIndex = 0; pairIndex < pairCount; ++pairIndex)
            {
                const PxTriggerPair& pair = pairs[pairIndex];
                if (pair.triggerShape == nullptr || pair.otherShape == nullptr
                    || pair.flags.isSet(PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER)
                    || pair.flags.isSet(PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
                    continue;

                auto* const triggerCollider =
                    static_cast<ColliderComponent*>(pair.triggerShape->userData);
                auto* const otherCollider =
                    static_cast<ColliderComponent*>(pair.otherShape->userData);
                if (triggerCollider == nullptr || otherCollider == nullptr)
                    continue;

                if (pair.status == PxPairFlag::eNOTIFY_TOUCH_FOUND)
                    addTriggerPair(*triggerCollider, *otherCollider);
                else if (pair.status == PxPairFlag::eNOTIFY_TOUCH_LOST)
                    removeTriggerPair(*triggerCollider, *otherCollider);
            }
        }

        void onAdvance(
            const PxRigidBody* const* bodyBuffer,
            const PxTransform* poseBuffer,
            const PxU32 count) override
        {
            GE_UNUSED(bodyBuffer);
            GE_UNUSED(poseBuffer);
            GE_UNUSED(count);
        }

        PxRigidStatic* createStaticActor(GameObject& gameObject) noexcept
        {
            if (const auto found = staticActors.find(&gameObject); found != staticActors.end())
                return found->second;

            PxRigidStatic* const actor = physics->createRigidStatic(
                PhysicsConversions::toPxPose(gameObject.getWorldTransform()));
            if (actor == nullptr)
            {
                LOG_ERROR("[PhysicsWorld] PhysX failed to create a static actor.");
                return nullptr;
            }

            scene->addActor(*actor);
            try
            {
                staticActors.emplace(&gameObject, actor);
            }
            catch (const std::bad_alloc&)
            {
                scene->removeActor(*actor);
                actor->release();
                LOG_ERROR("[PhysicsWorld] Failed to track a static actor because memory allocation failed.");
                return nullptr;
            }
            catch (const std::length_error&)
            {
                scene->removeActor(*actor);
                actor->release();
                LOG_ERROR("[PhysicsWorld] Static actor collection reached its maximum capacity.");
                return nullptr;
            }
            return actor;
        }

        bool setShapeGeometry(ColliderRecord& record) noexcept
        {
            if (record.component == nullptr || record.shape == nullptr)
                return false;

            GameObject* const gameObject = record.component->getGameObject();
            if (gameObject == nullptr)
                return false;

            const Vector3& scale = gameObject->getWorldScale();
            record.lastWorldScale = scale;
            const PhysicsShapeGeometry geometry = record.component->geometryForPhysics(scale);
            if (!isValidGeometry(geometry))
            {
                LOG_ERROR("[PhysicsWorld] Collider geometry or GameObject scale is invalid.");
                return false;
            }

            const PhysicsMaterial& material = record.component->getMaterial();
            if (record.staticFriction != material.getStaticFriction()
                || record.dynamicFriction != material.getDynamicFriction()
                || record.restitution != material.getRestitution())
            {
                PxMaterial* const pxMaterial = physics->createMaterial(
                    material.getStaticFriction(),
                    material.getDynamicFriction(),
                    material.getRestitution());
                if (pxMaterial == nullptr)
                {
                    LOG_ERROR("[PhysicsWorld] PhysX failed to create the Collider material.");
                    return false;
                }
                record.shape->setMaterials(&pxMaterial, 1);
                pxMaterial->release();
                record.staticFriction = material.getStaticFriction();
                record.dynamicFriction = material.getDynamicFriction();
                record.restitution = material.getRestitution();
            }

            PxTriangleMesh* triangleMesh = nullptr;
            PxConvexMesh* convexMesh = nullptr;
            const PxGeometryHolder pxGeometry =
                toPxGeometry(geometry, *physics, triangleMesh, convexMesh);
            if ((geometry.type == PhysicsShapeType::TriangleMesh && triangleMesh == nullptr)
                || (geometry.type == PhysicsShapeType::ConvexMesh && convexMesh == nullptr))
            {
                LOG_ERROR("[PhysicsWorld] Failed to load cooked mesh data for a Collider.");
                return false;
            }
            record.shape->setGeometry(pxGeometry.any());
            if (triangleMesh != nullptr)
                triangleMesh->release();
            if (convexMesh != nullptr)
                convexMesh->release();
            record.shape->setLocalPose(toShapePose(*record.component, scale, geometry.type));

            PxFilterData filterData = record.shape->getSimulationFilterData();
            const PxU32 oldLayer = filterData.word0;
            const PxU32 oldMask = filterData.word1;
            filterData.word0 = static_cast<PxU32>(record.component->getLayer());
            filterData.word1 = record.component->getCollisionMask();
            record.shape->setSimulationFilterData(filterData);
            record.shape->setQueryFilterData(filterData);

            setShapeTypeFlags(*record.shape, record.component->isTrigger());
            if (record.actor != nullptr
                && (oldLayer != filterData.word0 || oldMask != filterData.word1))
                scene->resetFiltering(*record.actor);
            return true;
        }

        void removeEmptyStaticActor(GameObject* const gameObject) noexcept
        {
            if (gameObject == nullptr)
                return;

            const auto actorIt = staticActors.find(gameObject);
            if (actorIt == staticActors.end())
                return;

            const PxRigidStatic* const actor = actorIt->second;
            const bool hasAttachedShapes = std::any_of(
                colliders.begin(),
                colliders.end(),
                [actor](const auto& entry) { return entry.second.actor == actor; });
            if (!hasAttachedShapes)
            {
                scene->removeActor(*actorIt->second);
                actorIt->second->release();
                staticActors.erase(actorIt);
            }
        }

        void moveColliders(GameObject& gameObject, PxRigidActor& destination) noexcept
        {
            PxRigidStatic* const sourceStatic = [&]() -> PxRigidStatic*
            {
                const auto found = staticActors.find(&gameObject);
                return found == staticActors.end() ? nullptr : found->second;
            }();

            for (auto& [component, record] : colliders)
            {
                if (component->getGameObject() != &gameObject || record.shape == nullptr
                    || record.actor == &destination)
                    continue;

                PxRigidActor* const source = record.actor;
                if (source != nullptr)
                    source->detachShape(*record.shape);

                if (!destination.attachShape(*record.shape))
                {
                    LOG_ERROR("[PhysicsWorld] Failed to attach a Collider Shape to its new actor.");
                    if (source != nullptr && !source->attachShape(*record.shape))
                        LOG_ERROR("[PhysicsWorld] Failed to restore a Collider Shape to its previous actor.");
                    continue;
                }
                record.actor = &destination;
            }

            if (sourceStatic != nullptr && sourceStatic != &destination)
                removeEmptyStaticActor(&gameObject);
        }
    };

    PhysicsWorld::PhysicsWorld() = default;

    PhysicsWorld::~PhysicsWorld()
    {
        shutdown();
    }

    bool PhysicsWorld::initialize() noexcept
    {
        if (isInitialized())
            return true;

        try
        {
            m_impl = std::make_unique<Impl>();
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsWorld] Failed to allocate PhysX state.");
            return false;
        }

        Impl& impl = *m_impl;
        try
        {
            impl.debugContacts.reserve(256);
            impl.debugRays.reserve(128);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsWorld] Failed to reserve debug visualization buffers.");
            shutdown();
            return false;
        }
        impl.foundation = PxCreateFoundation(PX_PHYSICS_VERSION, impl.allocator, impl.errorCallback);
        if (impl.foundation == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Failed to create PxFoundation.");
            shutdown();
            return false;
        }

        impl.physics = PxCreatePhysics(
            PX_PHYSICS_VERSION,
            *impl.foundation,
            PxTolerancesScale(),
            true,
            nullptr);
        if (impl.physics == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Failed to create PxPhysics.");
            shutdown();
            return false;
        }

        impl.extensionsInitialized = PxInitExtensions(*impl.physics, nullptr);
        if (!impl.extensionsInitialized)
        {
            LOG_ERROR("[PhysicsWorld] Failed to initialize PhysX extensions.");
            shutdown();
            return false;
        }

        impl.dispatcher = PxDefaultCpuDispatcherCreate(DISPATCHER_THREAD_COUNT);
        if (impl.dispatcher == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Failed to create the PhysX CPU dispatcher.");
            shutdown();
            return false;
        }

        PxSceneDesc sceneDescription(impl.physics->getTolerancesScale());
        sceneDescription.gravity = PhysicsConversions::toPx(impl.gravity);
        sceneDescription.cpuDispatcher = impl.dispatcher;
        sceneDescription.filterShader = physicsSimulationFilterShader;
        sceneDescription.simulationEventCallback = &impl;
        impl.scene = impl.physics->createScene(sceneDescription);
        if (impl.scene == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Failed to create PxScene.");
            shutdown();
            return false;
        }

        impl.controllerManager = PxCreateControllerManager(*impl.scene);
        if (impl.controllerManager == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Failed to create the PhysX Character Controller manager.");
            shutdown();
            return false;
        }

        return true;
    }

    void PhysicsWorld::shutdown() noexcept
    {
        if (m_impl == nullptr)
            return;

        Impl& impl = *m_impl;
        if (impl.scene != nullptr && impl.simulationPending)
        {
            if (!impl.scene->fetchResults(true))
                LOG_ERROR("[PhysicsWorld] Failed to finish pending simulation during shutdown.");
            impl.simulationPending = false;
        }

        for (auto& [component, record] : impl.joints)
        {
            GE_UNUSED(component);
            if (record.joint != nullptr)
                record.joint->release();
        }
        impl.joints.clear();

        if (impl.controllerManager != nullptr)
        {
            for (auto& [component, record] : impl.controllers)
            {
                GE_UNUSED(record);
                if (component != nullptr)
                    component->m_isGrounded = false;
            }
            impl.controllers.clear();
            impl.controllerManager->release();
            impl.controllerManager = nullptr;
        }

        for (auto& [gameObject, actor] : impl.rigidbodies)
        {
            GE_UNUSED(gameObject);
            if (actor.actor != nullptr)
            {
                if (impl.scene != nullptr)
                    impl.scene->removeActor(*actor.actor);
                actor.actor->release();
            }
        }
        impl.rigidbodies.clear();

        for (auto& [gameObject, actor] : impl.staticActors)
        {
            GE_UNUSED(gameObject);
            if (actor != nullptr)
            {
                if (impl.scene != nullptr)
                    impl.scene->removeActor(*actor);
                actor->release();
            }
        }
        impl.staticActors.clear();

        for (auto& [component, record] : impl.colliders)
        {
            GE_UNUSED(component);
            if (record.shape != nullptr)
                record.shape->release();
        }
        impl.colliders.clear();

        if (impl.scene != nullptr)
        {
            impl.scene->release();
            impl.scene = nullptr;
        }
        if (impl.dispatcher != nullptr)
        {
            impl.dispatcher->release();
            impl.dispatcher = nullptr;
        }
        if (impl.physics != nullptr)
        {
            if (impl.extensionsInitialized)
            {
                PxCloseExtensions();
                impl.extensionsInitialized = false;
            }
            impl.physics->release();
            impl.physics = nullptr;
        }
        if (impl.foundation != nullptr)
        {
            impl.foundation->release();
            impl.foundation = nullptr;
        }
        m_impl.reset();
    }

    bool PhysicsWorld::isInitialized() const noexcept
    {
        return m_impl != nullptr && m_impl->scene != nullptr;
    }

    void PhysicsWorld::simulate(const float fixedDeltaTime) noexcept
    {
        if (!isInitialized())
        {
            LOG_ERROR("[PhysicsWorld] Cannot simulate an uninitialized PhysicsWorld.");
            return;
        }
        if (!std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f)
        {
            LOG_ERROR("[PhysicsWorld] Fixed timestep must be a positive finite value.");
            return;
        }
        if (m_impl->simulationPending)
        {
            LOG_ERROR("[PhysicsWorld] Cannot start a new simulation before fetching the previous result.");
            return;
        }

        for (auto& [component, record] : m_impl->colliders)
        {
            if (component == nullptr || component->getGameObject() == nullptr)
                continue;

            const Vector3& scale = component->getGameObject()->getWorldScale();
            if (record.lastWorldScale.x != scale.x
                || record.lastWorldScale.y != scale.y
                || record.lastWorldScale.z != scale.z)
                m_impl->setShapeGeometry(record);
        }

        for (const auto& [gameObject, actor] : m_impl->staticActors)
        {
            if (gameObject != nullptr && actor != nullptr)
                actor->setGlobalPose(PhysicsConversions::toPxPose(gameObject->getWorldTransform()));
        }
        for (const auto& [gameObject, record] : m_impl->rigidbodies)
        {
            GE_UNUSED(gameObject);
            if (record.component == nullptr || record.actor == nullptr || !record.component->m_isKinematic)
                continue;
            GameObject* const object = record.component->getGameObject();
            if (object != nullptr)
                record.actor->setKinematicTarget(PhysicsConversions::toPxPose(object->getWorldTransform()));
        }

        {
            const std::lock_guard lock(m_impl->debugMutex);
            m_impl->debugContacts.clear();
        }
        m_impl->scene->simulate(fixedDeltaTime);
        m_impl->simulationPending = true;
    }

    void PhysicsWorld::fetchResults() noexcept
    {
        if (!isInitialized() || !m_impl->simulationPending)
            return;

        if (!m_impl->scene->fetchResults(true))
        {
            LOG_ERROR("[PhysicsWorld] PhysX failed to fetch simulation results.");
            m_impl->simulationPending = false;
            return;
        }
        m_impl->simulationPending = false;

        for (auto& [gameObject, record] : m_impl->rigidbodies)
        {
            GE_UNUSED(gameObject);
            if (record.component == nullptr || record.actor == nullptr || record.component->m_isKinematic)
                continue;

            const PxTransform pose = record.actor->getGlobalPose();
            record.component->m_linearVelocity = PhysicsConversions::fromPx(record.actor->getLinearVelocity());
            record.component->m_angularVelocity = PhysicsConversions::fromPx(record.actor->getAngularVelocity());
            record.component->synchronizeTransform(
                PhysicsConversions::fromPx(pose.p),
                PhysicsConversions::fromPx(pose.q));
        }
    }

    void PhysicsWorld::dispatchEvents() noexcept
    {
        if (m_impl == nullptr)
            return;

        std::vector<QueuedPhysicsEvent> events;
        {
            const std::lock_guard lock(m_impl->eventMutex);
            m_impl->appendTriggerStayEvents();
            m_impl->resetContactStayEvents();
            events.swap(m_impl->pendingEvents);
        }

        SceneManager& sceneManager = SceneManager::instance();
        const auto findGameObject = [&sceneManager](const ObjectGUID& guid) -> GameObject*
        {
            if (Scene* const activeScene = sceneManager.getActiveScene())
            {
                if (GameObject* const object = activeScene->find(guid))
                    return object;
            }
            if (Scene* const persistentScene = sceneManager.getPersistentScene())
                return persistentScene->find(guid);
            return nullptr;
        };

        for (const QueuedPhysicsEvent& event : events)
        {
            GameObject* const receiver = findGameObject(event.receiver);
            GameObject* const other = findGameObject(event.other);
            if (receiver == nullptr || other == nullptr || !receiver->isActiveInHierarchy())
                continue;

            receiver->forEachComponent([&](Component& component, const std::type_index&)
            {
                if (!component.isEnabled())
                    return;
                auto* const listener = dynamic_cast<PhysicsEventListener*>(&component);
                if (listener == nullptr)
                    return;

                try
                {
                    switch (event.type)
                    {
                    case PhysicsEventType::CollisionEnter:
                        listener->onCollisionEnter({ other, event.point, event.normal, event.impulse });
                        break;
                    case PhysicsEventType::CollisionStay:
                        listener->onCollisionStay({ other, event.point, event.normal, event.impulse });
                        break;
                    case PhysicsEventType::CollisionExit:
                        listener->onCollisionExit({ other, event.point, event.normal, event.impulse });
                        break;
                    case PhysicsEventType::TriggerEnter:
                        listener->onTriggerEnter(*other);
                        break;
                    case PhysicsEventType::TriggerStay:
                        listener->onTriggerStay(*other);
                        break;
                    case PhysicsEventType::TriggerExit:
                        listener->onTriggerExit(*other);
                        break;
                    }
                }
                catch (const std::exception& exception)
                {
                    LOG_ERROR("[PhysicsWorld] Physics event listener threw an exception: {}", exception.what());
                }
                catch (...)
                {
                    LOG_ERROR("[PhysicsWorld] Physics event listener threw an unknown exception.");
                }
            });
        }
    }

    void PhysicsWorld::setDebugCapture(const bool drawContacts, const bool drawRaycasts) noexcept
    {
        if (m_impl == nullptr)
            return;

        m_impl->captureDebugContacts.store(drawContacts, std::memory_order_relaxed);
        m_impl->captureDebugRays.store(drawRaycasts, std::memory_order_relaxed);
        if (!drawContacts || !drawRaycasts)
        {
            const std::lock_guard lock(m_impl->debugMutex);
            if (!drawContacts)
                m_impl->debugContacts.clear();
            if (!drawRaycasts)
                m_impl->debugRays.clear();
        }
    }

    void PhysicsWorld::drawDebug(const PhysicsDebugSettings& settings) const noexcept try
    {
        if (m_impl == nullptr)
            return;
        if (!settings.enabled || m_impl->scene == nullptr)
        {
            const std::lock_guard lock(m_impl->debugMutex);
            m_impl->debugContacts.clear();
            m_impl->debugRays.clear();
            return;
        }

        DebugPrimitive& debug = DebugPrimitive::instance();
        const Color colliderColor(0.2f, 0.9f, 0.35f, 1.0f);
        const Color aabbColor(1.0f, 0.75f, 0.15f, 1.0f);
        const Color velocityColor(0.2f, 0.65f, 1.0f, 1.0f);
        const auto appendAabb = [&debug](const PxBounds3& bounds, const Color& color)
        {
            const Vector3 min = PhysicsConversions::fromPx(bounds.minimum);
            const Vector3 max = PhysicsConversions::fromPx(bounds.maximum);
            const std::array<Vector3, 8> corners{
                Vector3(min.x, min.y, min.z), Vector3(max.x, min.y, min.z),
                Vector3(max.x, max.y, min.z), Vector3(min.x, max.y, min.z),
                Vector3(min.x, min.y, max.z), Vector3(max.x, min.y, max.z),
                Vector3(max.x, max.y, max.z), Vector3(min.x, max.y, max.z)
            };
            constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
                {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
                {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
                {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}}
            }};
            for (const auto& edge : edges)
                debug.drawLine(corners[edge[0]], corners[edge[1]], color);
        };

        for (const auto& [component, record] : m_impl->colliders)
        {
            GE_UNUSED(component);
            if (record.shape == nullptr || record.actor == nullptr)
                continue;

            const PxTransform pose = PxShapeExt::getGlobalPose(*record.shape, *record.actor);
            const Matrix world = Matrix::CreateFromQuaternion(PhysicsConversions::fromPx(pose.q))
                * Matrix::CreateTranslation(PhysicsConversions::fromPx(pose.p));
            const PxGeometry& geometry = record.shape->getGeometry();
            if (settings.drawColliders)
            {
                switch (geometry.getType())
                {
                case PxGeometryType::eBOX:
                {
                    const auto& box = static_cast<const PxBoxGeometry&>(geometry);
                    debug.drawBox(world, PhysicsConversions::fromPx(box.halfExtents), colliderColor);
                    break;
                }
                case PxGeometryType::eSPHERE:
                {
                    const auto& sphere = static_cast<const PxSphereGeometry&>(geometry);
                    debug.drawSphere(world, sphere.radius, colliderColor);
                    break;
                }
                case PxGeometryType::eCAPSULE:
                {
                    const auto& capsule = static_cast<const PxCapsuleGeometry&>(geometry);
                    const PxTransform actorPose = record.actor->getGlobalPose();
                    const Matrix capsuleWorld = Matrix::CreateFromQuaternion(
                        PhysicsConversions::fromPx(actorPose.q))
                        * Matrix::CreateTranslation(PhysicsConversions::fromPx(pose.p));
                    debug.drawCapsule(capsuleWorld, capsule.radius, capsule.halfHeight, colliderColor);
                    break;
                }
                case PxGeometryType::eTRIANGLEMESH:
                {
                    const auto& meshGeometry = static_cast<const PxTriangleMeshGeometry&>(geometry);
                    const PxTriangleMesh* const mesh = meshGeometry.triangleMesh;
                    if (mesh == nullptr)
                        break;
                    const PxU32 triangleCount = std::min<PxU32>(mesh->getNbTriangles(), 256);
                    const auto* const bytes = static_cast<const PxU8*>(mesh->getTriangles());
                    const bool use16BitIndices =
                        mesh->getTriangleMeshFlags().isSet(PxTriangleMeshFlag::e16_BIT_INDICES);
                    const PxU32 stride = use16BitIndices ? sizeof(PxU16) : sizeof(PxU32);
                    for (PxU32 triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
                    {
                        PxU32 indices[3]{};
                        for (PxU32 corner = 0; corner < 3; ++corner)
                        {
                            const std::size_t offset =
                                (static_cast<std::size_t>(triangleIndex) * 3 + corner) * stride;
                            if (use16BitIndices)
                            {
                                PxU16 index = 0;
                                std::memcpy(&index, bytes + offset, sizeof(index));
                                indices[corner] = index;
                            }
                            else
                                std::memcpy(&indices[corner], bytes + offset, sizeof(PxU32));
                        }
                        const auto getVertex = [&meshGeometry, &pose, mesh](const PxU32 index)
                        {
                            return PhysicsConversions::fromPx(
                                pose.transform(meshGeometry.scale.transform(mesh->getVertices()[index])));
                        };
                        const Vector3 first = getVertex(indices[0]);
                        const Vector3 second = getVertex(indices[1]);
                        const Vector3 third = getVertex(indices[2]);
                        debug.drawLine(first, second, colliderColor);
                        debug.drawLine(second, third, colliderColor);
                        debug.drawLine(third, first, colliderColor);
                    }
                    break;
                }
                case PxGeometryType::eCONVEXMESH:
                {
                    const auto& meshGeometry = static_cast<const PxConvexMeshGeometry&>(geometry);
                    const PxConvexMesh* const mesh = meshGeometry.convexMesh;
                    if (mesh == nullptr)
                        break;
                    const PxU32 polygonCount = std::min<PxU32>(mesh->getNbPolygons(), 128);
                    const PxU8* const indices = mesh->getIndexBuffer();
                    for (PxU32 polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
                    {
                        PxHullPolygon polygon;
                        if (!mesh->getPolygonData(polygonIndex, polygon) || polygon.mNbVerts < 3)
                            continue;
                        for (PxU32 vertexIndex = 0; vertexIndex < polygon.mNbVerts; ++vertexIndex)
                        {
                            const PxU32 nextIndex = (vertexIndex + 1) % polygon.mNbVerts;
                            const PxVec3 firstLocal = meshGeometry.scale.transform(
                                mesh->getVertices()[indices[polygon.mIndexBase + vertexIndex]]);
                            const PxVec3 secondLocal = meshGeometry.scale.transform(
                                mesh->getVertices()[indices[polygon.mIndexBase + nextIndex]]);
                            debug.drawLine(
                                PhysicsConversions::fromPx(pose.transform(firstLocal)),
                                PhysicsConversions::fromPx(pose.transform(secondLocal)),
                                colliderColor);
                        }
                    }
                    break;
                }
                default:
                    break;
                }
            }

            if (settings.drawAabbs)
                appendAabb(PxShapeExt::getWorldBounds(*record.shape, *record.actor, 1.0f), aabbColor);

            if (settings.drawVelocities)
            {
                if (const auto* const dynamicActor = record.actor->is<PxRigidDynamic>())
                {
                    const Vector3 position = PhysicsConversions::fromPx(dynamicActor->getGlobalPose().p);
                    const Vector3 velocity = PhysicsConversions::fromPx(dynamicActor->getLinearVelocity());
                    if (velocity.LengthSquared() > 0.000001f)
                        debug.drawLine(position, position + velocity * 0.25f, velocityColor);
                }
            }
            if (settings.drawCenterOfMass)
            {
                if (const auto* const dynamicActor = record.actor->is<PxRigidDynamic>())
                {
                    const PxTransform centerOfMass =
                        dynamicActor->getGlobalPose() * dynamicActor->getCMassLocalPose();
                    const Vector3 center = PhysicsConversions::fromPx(centerOfMass.p);
                    const Quaternion rotation = PhysicsConversions::fromPx(centerOfMass.q);
                    const Vector3 axisX = Vector3::Transform(Vector3(0.15f, 0.0f, 0.0f), rotation);
                    const Vector3 axisY = Vector3::Transform(Vector3(0.0f, 0.15f, 0.0f), rotation);
                    const Vector3 axisZ = Vector3::Transform(Vector3(0.0f, 0.0f, 0.15f), rotation);
                    debug.drawLine(center - axisX, center + axisX, Color(1.0f, 0.2f, 0.2f, 1.0f));
                    debug.drawLine(center - axisY, center + axisY, Color(0.2f, 1.0f, 0.2f, 1.0f));
                    debug.drawLine(center - axisZ, center + axisZ, Color(0.2f, 0.4f, 1.0f, 1.0f));
                }
            }
        }

        if (settings.drawColliders)
        {
            for (const auto& [controllerComponent, controllerRecord] : m_impl->controllers)
            {
                if (controllerComponent == nullptr || controllerRecord.controller == nullptr)
                    continue;
                const Vector3 center = controllerComponent->getPosition()
                    + Vector3(0.0f, controllerComponent->getHeight() * 0.5f, 0.0f);
                const Matrix controllerWorld = Matrix::CreateTranslation(center);
                const float halfHeight =
                    (controllerComponent->getHeight() * 0.5f) - controllerComponent->getRadius();
                debug.drawCapsule(controllerWorld, controllerComponent->getRadius(),
                    std::max(halfHeight, 0.0f), colliderColor);
            }
        }

        if (settings.drawAabbs)
        {
            for (const auto& [controllerComponent, controllerRecord] : m_impl->controllers)
            {
                GE_UNUSED(controllerComponent);
                if (controllerRecord.controller != nullptr && controllerRecord.controller->getActor() != nullptr)
                    appendAabb(controllerRecord.controller->getActor()->getWorldBounds(1.0f), aabbColor);
            }
        }

        if (settings.drawContacts)
        {
            const std::lock_guard lock(m_impl->debugMutex);
            const Color contactColor(1.0f, 0.2f, 0.15f, 1.0f);
            for (const Impl::DebugContact& contact : m_impl->debugContacts)
            {
                constexpr float markerSize = 0.06f;
                debug.drawLine(contact.point - Vector3(markerSize, 0.0f, 0.0f),
                    contact.point + Vector3(markerSize, 0.0f, 0.0f), contactColor);
                debug.drawLine(contact.point - Vector3(0.0f, markerSize, 0.0f),
                    contact.point + Vector3(0.0f, markerSize, 0.0f), contactColor);
                debug.drawLine(contact.point - Vector3(0.0f, 0.0f, markerSize),
                    contact.point + Vector3(0.0f, 0.0f, markerSize), contactColor);
                debug.drawLine(contact.point, contact.point + contact.normal * 0.35f, contactColor);
            }
        }

        std::vector<Impl::DebugRay> rays;
        {
            const std::lock_guard lock(m_impl->debugMutex);
            if (settings.drawRaycasts)
                rays.swap(m_impl->debugRays);
            else
                m_impl->debugRays.clear();
        }
        const Color rayColor(1.0f, 0.9f, 0.1f, 1.0f);
        for (const Impl::DebugRay& ray : rays)
            debug.drawLine(ray.start, ray.end, rayColor);
    }
    catch (const std::bad_alloc&)
    {
        LOG_ERROR("[PhysicsWorld] Failed to allocate debug visualization geometry.");
    }
    catch (const std::length_error&)
    {
        LOG_ERROR("[PhysicsWorld] Debug visualization exceeded a collection capacity.");
    }

    PhysicsWorldStatistics PhysicsWorld::getStatistics() const noexcept
    {
        PhysicsWorldStatistics statistics;
        if (m_impl == nullptr)
            return statistics;

        statistics.activeShapes = static_cast<std::uint32_t>(
            m_impl->colliders.size() + m_impl->controllers.size());
        statistics.activeJoints = static_cast<std::uint32_t>(m_impl->joints.size());
        statistics.activeActors = static_cast<std::uint32_t>(
            m_impl->rigidbodies.size() + m_impl->staticActors.size() + m_impl->controllers.size());
        statistics.dynamicBodies = static_cast<std::uint32_t>(m_impl->rigidbodies.size());
        statistics.staticBodies = static_cast<std::uint32_t>(m_impl->staticActors.size());
        statistics.activeContacts = static_cast<std::uint32_t>(m_impl->activeContactPairs.size());
        for (const auto& [gameObject, record] : m_impl->rigidbodies)
        {
            GE_UNUSED(gameObject);
            if (record.actor != nullptr && !record.actor->isSleeping())
                ++statistics.activeBodies;
        }
        return statistics;
    }

    bool PhysicsWorld::reset() noexcept
    {
        if (m_impl == nullptr || m_impl->scene == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Cannot reset an uninitialized Physics scene.");
            return false;
        }
        if (m_impl->simulationPending)
        {
            LOG_ERROR("[PhysicsWorld] Cannot reset while a simulation step is pending.");
            return false;
        }

        bool resetSuccessful = true;
        for (const auto& [gameObject, record] : m_impl->rigidbodies)
        {
            if (gameObject == nullptr || record.component == nullptr || record.actor == nullptr)
                continue;
            if (gameObject->getTransform() == nullptr)
            {
                LOG_ERROR("[PhysicsWorld] Cannot reset a Rigidbody whose GameObject has no Transform.");
                resetSuccessful = false;
                continue;
            }
            record.component->synchronizeTransform(
                PhysicsConversions::fromPx(record.initialPose.p),
                PhysicsConversions::fromPx(record.initialPose.q));
            const bool isKinematic = record.actor->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC);
            record.actor->setGlobalPose(record.initialPose, !isKinematic);
            if (!isKinematic)
            {
                record.actor->setLinearVelocity(PxVec3(0.0f));
                record.actor->setAngularVelocity(PxVec3(0.0f));
                record.actor->clearForce(PxForceMode::eFORCE);
                record.actor->clearTorque(PxForceMode::eFORCE);
                record.actor->wakeUp();
            }
            m_impl->scene->resetFiltering(*record.actor);
            record.component->m_linearVelocity = Vector3::Zero;
            record.component->m_angularVelocity = Vector3::Zero;
        }

        for (const auto& [gameObject, actor] : m_impl->staticActors)
        {
            if (gameObject == nullptr || actor == nullptr)
                continue;
            actor->setGlobalPose(PhysicsConversions::toPxPose(gameObject->getWorldTransform()), true);
            m_impl->scene->resetFiltering(*actor);
        }

        for (auto& [component, record] : m_impl->controllers)
        {
            if (component == nullptr || record.controller == nullptr)
                continue;
            GameObject* const gameObject = component->getGameObject();
            if (gameObject == nullptr)
                continue;

            Transform* const localTransform = gameObject->getTransform();
            if (localTransform == nullptr)
            {
                LOG_ERROR("[PhysicsWorld] Cannot reset a Character Controller whose GameObject has no Transform.");
                resetSuccessful = false;
                continue;
            }
            Vector3 localPosition = record.initialPosition;
            if (const GameObject* const parent = gameObject->getParent())
            {
                localPosition = Vector3::Transform(
                    record.initialPosition,
                    parent->getWorldTransform().toInverseMatrix());
            }
            localTransform->setPosition(localPosition);
            localTransform->setRotation(record.initialLocalRotation);
            const Vector3& position = record.initialPosition;
            record.controller->setFootPosition(PxExtendedVec3(
                static_cast<double>(position.x),
                static_cast<double>(position.y),
                static_cast<double>(position.z)));
            if (PxRigidDynamic* const actor = record.controller->getActor())
                m_impl->scene->resetFiltering(*actor);
            component->m_position = position;
            component->m_isGrounded = false;
        }

        {
            const std::lock_guard lock(m_impl->eventMutex);
            m_impl->pendingEvents.clear();
            m_impl->activeContactShapePairs.clear();
            m_impl->activeContactPairs.clear();
            m_impl->activeTriggerShapePairs.clear();
            m_impl->activeTriggerPairs.clear();
        }
        {
            const std::lock_guard lock(m_impl->debugMutex);
            m_impl->debugContacts.clear();
        }
        return resetSuccessful;
    }

    std::optional<RaycastHit> PhysicsWorld::raycast(
        const Vector3& origin,
        const Vector3& direction,
        const float maxDistance,
        const PhysicsQueryFilter& filter) const noexcept
    {
        if (!isInitialized())
        {
            LOG_ERROR("[PhysicsWorld] Cannot raycast before PhysicsWorld is initialized.");
            return std::nullopt;
        }
        if (!isFiniteVector(origin) || !isValidQueryDistance(maxDistance)
            || !isValidQueryFilter(filter))
        {
            LOG_ERROR("[PhysicsWorld] Raycast requires a finite origin, non-negative distance, and an enabled actor filter.");
            return std::nullopt;
        }

        PxVec3 normalizedDirection;
        if (!normalizeQueryDirection(direction, normalizedDirection))
        {
            LOG_ERROR("[PhysicsWorld] Raycast direction must be finite and non-zero.");
            return std::nullopt;
        }

        m_impl->recordDebugRay(origin, normalizedDirection, maxDistance);
        PhysicsQueryFilterCallback filterCallback(filter);
        PxRaycastBuffer hit;
        const bool hasHit = m_impl->scene->raycast(
            PhysicsConversions::toPx(origin),
            normalizedDirection,
            maxDistance,
            hit,
            PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
            makeQueryFilterData(filter),
            &filterCallback);
        if (!hasHit || !hit.hasBlock)
            return std::nullopt;

        RaycastHit result;
        if (!convertQueryHit(hit.block, result))
        {
            LOG_ERROR("[PhysicsWorld] Raycast returned a hit without a valid Collider.");
            return std::nullopt;
        }
        return result;
    }

    std::vector<RaycastHit> PhysicsWorld::raycastAll(
        const Vector3& origin,
        const Vector3& direction,
        const float maxDistance,
        const std::uint32_t maxHits,
        const PhysicsQueryFilter& filter) const noexcept
    {
        std::vector<RaycastHit> results;
        if (!isInitialized() || !isFiniteVector(origin) || !isValidQueryDistance(maxDistance)
            || maxHits == 0 || !isValidQueryFilter(filter))
        {
            if (maxHits == 0 || !isValidQueryFilter(filter))
                LOG_ERROR("[PhysicsWorld] RaycastAll requires a positive hit limit and an enabled actor filter.");
            return results;
        }

        PxVec3 normalizedDirection;
        if (!normalizeQueryDirection(direction, normalizedDirection))
        {
            LOG_ERROR("[PhysicsWorld] RaycastAll direction must be finite and non-zero.");
            return results;
        }

        m_impl->recordDebugRay(origin, normalizedDirection, maxDistance);
        try
        {
            std::vector<PxRaycastHit> hitBuffer(maxHits);
            results.reserve(maxHits);
            PhysicsQueryFilterCallback filterCallback(filter);
            PxRaycastBuffer hits(hitBuffer.data(), static_cast<PxU32>(hitBuffer.size()));
            m_impl->scene->raycast(
                PhysicsConversions::toPx(origin),
                normalizedDirection,
                maxDistance,
                hits,
                PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
                makeQueryFilterData(filter, true),
                &filterCallback);

            for (PxU32 hitIndex = 0; hitIndex < hits.getNbTouches(); ++hitIndex)
            {
                RaycastHit result;
                if (convertQueryHit(hits.getTouch(hitIndex), result))
                    results.push_back(result);
            }
            std::sort(results.begin(), results.end(),
                [](const RaycastHit& lhs, const RaycastHit& rhs)
                {
                    return lhs.distance < rhs.distance;
                });
        }
        catch (const std::bad_alloc&)
        {
            results.clear();
            LOG_ERROR("[PhysicsWorld] Failed to allocate RaycastAll result buffers.");
        }
        catch (const std::length_error&)
        {
            results.clear();
            LOG_ERROR("[PhysicsWorld] RaycastAll hit limit is too large.");
        }
        return results;
    }

    std::optional<SweepHit> PhysicsWorld::sphereCast(
        const Vector3& origin,
        const float radius,
        const Vector3& direction,
        const float maxDistance,
        const PhysicsQueryFilter& filter) const noexcept
    {
        if (!isInitialized() || !isFiniteVector(origin) || !std::isfinite(radius) || radius <= 0.0f
            || !isValidQueryDistance(maxDistance) || !isValidQueryFilter(filter))
        {
            LOG_ERROR("[PhysicsWorld] SphereCast requires finite inputs, a positive radius, a non-negative distance, and an enabled actor filter.");
            return std::nullopt;
        }

        PxVec3 normalizedDirection;
        if (!normalizeQueryDirection(direction, normalizedDirection))
        {
            LOG_ERROR("[PhysicsWorld] SphereCast direction must be finite and non-zero.");
            return std::nullopt;
        }

        PhysicsQueryFilterCallback filterCallback(filter);
        PxSweepBuffer hit;
        const PxSphereGeometry geometry(radius);
        const bool hasHit = m_impl->scene->sweep(
            geometry,
            PxTransform(PhysicsConversions::toPx(origin)),
            normalizedDirection,
            maxDistance,
            hit,
            PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
            makeQueryFilterData(filter),
            &filterCallback);
        if (!hasHit || !hit.hasBlock)
            return std::nullopt;

        SweepHit result;
        if (!convertQueryHit(hit.block, result))
        {
            LOG_ERROR("[PhysicsWorld] SphereCast returned a hit without a valid Collider.");
            return std::nullopt;
        }
        return result;
    }

    std::optional<SweepHit> PhysicsWorld::capsuleCast(
        const Vector3& center,
        const float radius,
        const float halfHeight,
        const Vector3& direction,
        const float maxDistance,
        const PhysicsQueryFilter& filter) const noexcept
    {
        if (!isInitialized() || !isFiniteVector(center) || !std::isfinite(radius) || radius <= 0.0f
            || !std::isfinite(halfHeight) || halfHeight < 0.0f
            || !isValidQueryDistance(maxDistance) || !isValidQueryFilter(filter))
        {
            LOG_ERROR("[PhysicsWorld] CapsuleCast requires finite inputs, a positive radius, a non-negative half-height and distance, and an enabled actor filter.");
            return std::nullopt;
        }

        PxVec3 normalizedDirection;
        if (!normalizeQueryDirection(direction, normalizedDirection))
        {
            LOG_ERROR("[PhysicsWorld] CapsuleCast direction must be finite and non-zero.");
            return std::nullopt;
        }

        PhysicsQueryFilterCallback filterCallback(filter);
        PxSweepBuffer hit;
        const PxCapsuleGeometry geometry(radius, halfHeight);
        const PxQuat capsuleRotation(PxHalfPi, PxVec3(0.0f, 0.0f, 1.0f));
        const bool hasHit = m_impl->scene->sweep(
            geometry,
            PxTransform(PhysicsConversions::toPx(center), capsuleRotation),
            normalizedDirection,
            maxDistance,
            hit,
            PxHitFlag::ePOSITION | PxHitFlag::eNORMAL,
            makeQueryFilterData(filter),
            &filterCallback);
        if (!hasHit || !hit.hasBlock)
            return std::nullopt;

        SweepHit result;
        if (!convertQueryHit(hit.block, result))
        {
            LOG_ERROR("[PhysicsWorld] CapsuleCast returned a hit without a valid Collider.");
            return std::nullopt;
        }
        return result;
    }

    PhysicsOverlapResults PhysicsWorld::overlapSphere(
        const Vector3& center,
        const float radius,
        const std::uint32_t maxHits,
        const PhysicsQueryFilter& filter) const noexcept
    {
        PhysicsOverlapResults results;
        if (!isInitialized() || !isFiniteVector(center) || !std::isfinite(radius) || radius <= 0.0f
            || maxHits == 0 || !isValidQueryFilter(filter))
        {
            if (maxHits == 0 || !isValidQueryFilter(filter))
                LOG_ERROR("[PhysicsWorld] OverlapSphere requires a positive hit limit and an enabled actor filter.");
            return results;
        }

        try
        {
            std::vector<PxOverlapHit> hitBuffer(maxHits);
            results.reserve((std::min)(maxHits, static_cast<std::uint32_t>(hitBuffer.size())));
            std::unordered_set<GameObjectHandle, ObjectGUIDHash> uniqueObjects;
            uniqueObjects.reserve(results.capacity());

            PhysicsQueryFilterCallback filterCallback(filter);
            PxOverlapBuffer hits(hitBuffer.data(), static_cast<PxU32>(hitBuffer.size()));
            m_impl->scene->overlap(
                PxSphereGeometry(radius),
                PxTransform(PhysicsConversions::toPx(center)),
                hits,
                makeQueryFilterData(filter, true),
                &filterCallback);

            for (PxU32 hitIndex = 0; hitIndex < hits.getNbTouches(); ++hitIndex)
            {
                const PxOverlapHit& hit = hits.getTouch(hitIndex);
                if (hit.shape == nullptr || hit.shape->userData == nullptr)
                    continue;

                const auto* const collider = static_cast<const ColliderComponent*>(hit.shape->userData);
                const GameObject* const gameObject = collider->getGameObject();
                if (gameObject == nullptr)
                    continue;

                const GameObjectHandle guid = gameObject->getGUID();
                if (uniqueObjects.emplace(guid).second)
                    results.push_back(guid);
            }
        }
        catch (const std::bad_alloc&)
        {
            results.clear();
            LOG_ERROR("[PhysicsWorld] Failed to allocate OverlapSphere result buffers.");
        }
        catch (const std::length_error&)
        {
            results.clear();
            LOG_ERROR("[PhysicsWorld] OverlapSphere hit limit is too large.");
        }
        return results;
    }

    Vector3 PhysicsWorld::getGravity() const noexcept
    {
        return m_impl != nullptr ? m_impl->gravity : Vector3(0.0f, DEFAULT_GRAVITY, 0.0f);
    }

    void PhysicsWorld::setGravity(const Vector3& gravity) noexcept
    {
        if (!isFiniteVector(gravity))
        {
            LOG_ERROR("[PhysicsWorld] Gravity must be finite.");
            return;
        }
        if (m_impl == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Cannot set gravity before initialization.");
            return;
        }

        m_impl->gravity = gravity;
        if (m_impl->scene != nullptr)
            m_impl->scene->setGravity(PhysicsConversions::toPx(gravity));
    }

    bool PhysicsWorld::createRigidbody(RigidbodyComponent& component) noexcept
    {
        if (!isInitialized() || component.getGameObject() == nullptr)
            return false;
        if (m_impl->simulationPending)
        {
            LOG_ERROR("[PhysicsWorld] Cannot create a Rigidbody while simulating.");
            return false;
        }

        GameObject* const gameObject = component.getGameObject();
        if (m_impl->rigidbodies.contains(gameObject))
            return true;
        const bool hasTriangleMesh = std::any_of(
            m_impl->colliders.begin(),
            m_impl->colliders.end(),
            [gameObject](const auto& entry)
            {
                return entry.first != nullptr
                    && entry.first->getGameObject() == gameObject
                    && dynamic_cast<const MeshColliderComponent*>(entry.first) != nullptr;
            });
        if (hasTriangleMesh)
        {
            LOG_ERROR("[PhysicsWorld] A GameObject with a triangle mesh Collider cannot receive a Rigidbody.");
            return false;
        }

        PxRigidDynamic* const actor = m_impl->physics->createRigidDynamic(
            PhysicsConversions::toPxPose(gameObject->getWorldTransform()));
        if (actor == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] PhysX failed to create a Dynamic actor.");
            return false;
        }

        actor->setMass(component.m_mass);
        actor->setLinearDamping(component.m_linearDamping);
        actor->setAngularDamping(component.m_angularDamping);
        actor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, component.m_isKinematic);
        if (!component.m_isKinematic)
        {
            actor->setLinearVelocity(PhysicsConversions::toPx(component.m_linearVelocity));
            actor->setAngularVelocity(PhysicsConversions::toPx(component.m_angularVelocity));
        }
        actor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !component.m_gravityEnabled);
        m_impl->scene->addActor(*actor);

        try
        {
            const Transform* const localTransform = gameObject->getTransform();
            if (localTransform == nullptr)
            {
                m_impl->scene->removeActor(*actor);
                actor->release();
                LOG_ERROR("[PhysicsWorld] Cannot create a Rigidbody without a Transform.");
                return false;
            }
            m_impl->rigidbodies.emplace(gameObject, Impl::RigidbodyRecord{
                &component,
                actor,
                PhysicsConversions::toPxPose(gameObject->getWorldTransform()) });
        }
        catch (const std::bad_alloc&)
        {
            m_impl->scene->removeActor(*actor);
            actor->release();
            LOG_ERROR("[PhysicsWorld] Failed to track a Rigidbody because memory allocation failed.");
            return false;
        }
        catch (const std::length_error&)
        {
            m_impl->scene->removeActor(*actor);
            actor->release();
            LOG_ERROR("[PhysicsWorld] Rigidbody collection reached its maximum capacity.");
            return false;
        }

        m_impl->moveColliders(*gameObject, *actor);
        if (!component.m_isKinematic && actor->getNbShapes() > 0)
            PxRigidBodyExt::updateMassAndInertia(*actor, component.m_mass);
        for (auto& [jointComponent, jointRecord] : m_impl->joints)
        {
            GE_UNUSED(jointRecord);
            if (jointComponent != nullptr)
                updateJoint(*jointComponent);
        }
        return true;
    }

    void PhysicsWorld::destroyRigidbody(RigidbodyComponent& component) noexcept
    {
        if (m_impl == nullptr || component.getGameObject() == nullptr || m_impl->simulationPending)
            return;

        GameObject* const gameObject = component.getGameObject();
        const auto found = m_impl->rigidbodies.find(gameObject);
        if (found == m_impl->rigidbodies.end())
            return;

        for (auto& [jointComponent, jointRecord] : m_impl->joints)
        {
            GE_UNUSED(jointComponent);
            if (jointRecord.joint != nullptr
                && (jointRecord.bodyA == found->second.component
                    || jointRecord.bodyB == found->second.component))
            {
                jointRecord.joint->release();
                jointRecord.joint = nullptr;
            }
        }

        const bool hasColliders = std::any_of(
            m_impl->colliders.begin(),
            m_impl->colliders.end(),
            [gameObject](const auto& entry)
            {
                return entry.first != nullptr && entry.first->getGameObject() == gameObject;
            });
        PxRigidStatic* const replacementActor = hasColliders
            ? m_impl->createStaticActor(*gameObject)
            : nullptr;
        if (replacementActor != nullptr)
            m_impl->moveColliders(*gameObject, *replacementActor);

        if (m_impl->scene != nullptr)
            m_impl->scene->removeActor(*found->second.actor);
        found->second.actor->release();
        m_impl->rigidbodies.erase(found);
    }

    void PhysicsWorld::forgetRigidbodyReferences(RigidbodyComponent& component) noexcept
    {
        if (m_impl == nullptr)
            return;

        SceneManager::instance().forEachScene([this, &component](Scene& scene)
        {
            for (const auto& object : scene.getGameObjects())
            {
                if (object == nullptr)
                    continue;
                JointComponent* const jointComponent = object->getComponent<JointComponent>();
                if (jointComponent == nullptr || jointComponent->m_connectedBody != &component)
                    continue;

                jointComponent->m_connectedBody = nullptr;
                jointComponent->m_pendingConnectedBodyGuid = {};
                const auto joint = m_impl->joints.find(jointComponent);
                if (joint == m_impl->joints.end())
                    continue;
                if (joint->second.joint != nullptr)
                {
                    joint->second.joint->release();
                    joint->second.joint = nullptr;
                }
                joint->second.bodyB = nullptr;
                if (joint->second.bodyA != &component)
                    updateJoint(*jointComponent);
            }
        });
    }

    void PhysicsWorld::updateRigidbody(RigidbodyComponent& component) noexcept
    {
        if (m_impl == nullptr || component.getGameObject() == nullptr)
            return;
        const auto found = m_impl->rigidbodies.find(component.getGameObject());
        if (found == m_impl->rigidbodies.end() || found->second.actor == nullptr)
            return;

        PxRigidDynamic& actor = *found->second.actor;
        actor.setMass(component.m_mass);
        actor.setLinearDamping(component.m_linearDamping);
        actor.setAngularDamping(component.m_angularDamping);
        actor.setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, component.m_isKinematic);
        actor.setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !component.m_gravityEnabled);
        if (!component.m_isKinematic)
        {
            actor.setLinearVelocity(PhysicsConversions::toPx(component.m_linearVelocity));
            actor.setAngularVelocity(PhysicsConversions::toPx(component.m_angularVelocity));
            if (actor.getNbShapes() > 0)
                PxRigidBodyExt::updateMassAndInertia(actor, component.m_mass);
        }
    }

    void PhysicsWorld::setSleeping(RigidbodyComponent& component, const bool sleeping) noexcept
    {
        if (m_impl == nullptr || component.getGameObject() == nullptr || m_impl->simulationPending)
            return;
        const auto found = m_impl->rigidbodies.find(component.getGameObject());
        if (found == m_impl->rigidbodies.end() || found->second.actor == nullptr)
            return;

        if (found->second.actor->getRigidBodyFlags().isSet(PxRigidBodyFlag::eKINEMATIC))
        {
            LOG_ERROR("[PhysicsWorld] Cannot explicitly wake or sleep a Kinematic Rigidbody.");
            return;
        }

        if (sleeping)
            found->second.actor->putToSleep();
        else
            found->second.actor->wakeUp();
    }

    bool PhysicsWorld::addForce(
        RigidbodyComponent& component,
        const Vector3& force,
        const bool impulse) noexcept
    {
        if (!isFiniteVector(force) || m_impl == nullptr || component.getGameObject() == nullptr
            || m_impl->simulationPending)
            return false;
        const auto found = m_impl->rigidbodies.find(component.getGameObject());
        if (found == m_impl->rigidbodies.end() || found->second.actor == nullptr || component.m_isKinematic)
            return false;

        found->second.actor->addForce(
            PhysicsConversions::toPx(force),
            impulse ? PxForceMode::eIMPULSE : PxForceMode::eFORCE);
        return true;
    }

    bool PhysicsWorld::addTorque(RigidbodyComponent& component, const Vector3& torque) noexcept
    {
        if (!isFiniteVector(torque) || m_impl == nullptr || component.getGameObject() == nullptr
            || m_impl->simulationPending)
            return false;
        const auto found = m_impl->rigidbodies.find(component.getGameObject());
        if (found == m_impl->rigidbodies.end() || found->second.actor == nullptr || component.m_isKinematic)
            return false;

        found->second.actor->addTorque(PhysicsConversions::toPx(torque));
        return true;
    }

    bool PhysicsWorld::createCollider(ColliderComponent& component) noexcept
    {
        if (!isInitialized() || component.getGameObject() == nullptr || m_impl->simulationPending)
            return false;
        if (m_impl->colliders.contains(&component))
            return true;

        GameObject* const gameObject = component.getGameObject();
        const PhysicsShapeGeometry geometry = component.geometryForPhysics(gameObject->getWorldScale());
        if ((geometry.type == PhysicsShapeType::TriangleMesh || geometry.type == PhysicsShapeType::ConvexMesh)
            && geometry.meshAsset == nullptr)
        {
            LOG_INFO("[PhysicsWorld] Mesh Collider is waiting for a cooked Physics asset.");
            return true;
        }
        if (!isValidGeometry(geometry))
        {
            LOG_ERROR("[PhysicsWorld] Cannot create a Collider with invalid geometry or scale.");
            return false;
        }
        const bool isTriangleMesh = geometry.type == PhysicsShapeType::TriangleMesh;
        if ((isTriangleMesh && component.isTrigger())
            || (isTriangleMesh && m_impl->rigidbodies.contains(gameObject)))
        {
            LOG_ERROR("[PhysicsWorld] Triangle mesh Colliders must be non-trigger shapes on static actors.");
            return false;
        }

        const PhysicsMaterial& material = component.getMaterial();
        PxMaterial* const pxMaterial = m_impl->physics->createMaterial(
            material.getStaticFriction(),
            material.getDynamicFriction(),
            material.getRestitution());
        if (pxMaterial == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] PhysX failed to create the Collider material.");
            return false;
        }

        PxTriangleMesh* triangleMesh = nullptr;
        PxConvexMesh* convexMesh = nullptr;
        const PxGeometryHolder pxGeometry =
            toPxGeometry(geometry, *m_impl->physics, triangleMesh, convexMesh);
        if ((geometry.type == PhysicsShapeType::TriangleMesh && triangleMesh == nullptr)
            || (geometry.type == PhysicsShapeType::ConvexMesh && convexMesh == nullptr))
        {
            pxMaterial->release();
            LOG_ERROR("[PhysicsWorld] Failed to load cooked mesh data for a Collider.");
            return false;
        }
        PxShape* const shape = m_impl->physics->createShape(pxGeometry.any(), *pxMaterial, true);
        if (triangleMesh != nullptr)
            triangleMesh->release();
        if (convexMesh != nullptr)
            convexMesh->release();
        if (shape == nullptr)
        {
            pxMaterial->release();
            LOG_ERROR("[PhysicsWorld] PhysX failed to create a Collider Shape.");
            return false;
        }
        shape->setMaterials(&pxMaterial, 1);
        pxMaterial->release();

        shape->userData = &component;
        PxFilterData filterData;
        filterData.word0 = static_cast<PxU32>(component.getLayer());
        filterData.word1 = component.getCollisionMask();
        shape->setSimulationFilterData(filterData);
        shape->setQueryFilterData(filterData);
        const Vector3& scale = gameObject->getWorldScale();
        shape->setLocalPose(toShapePose(component, scale, geometry.type));
        setShapeTypeFlags(*shape, component.isTrigger());

        PxRigidActor* actor = nullptr;
        if (const auto body = m_impl->rigidbodies.find(gameObject); body != m_impl->rigidbodies.end())
            actor = body->second.actor;
        else
            actor = m_impl->createStaticActor(*gameObject);

        if (actor == nullptr || !actor->attachShape(*shape))
        {
            shape->release();
            LOG_ERROR("[PhysicsWorld] Failed to attach the Collider Shape to a Physics actor.");
            return false;
        }

        try
        {
            m_impl->colliders.emplace(&component, Impl::ColliderRecord{
                &component,
                shape,
                actor,
                material.getStaticFriction(),
                material.getDynamicFriction(),
                material.getRestitution(),
                scale });
        }
        catch (const std::bad_alloc&)
        {
            actor->detachShape(*shape);
            shape->release();
            m_impl->removeEmptyStaticActor(gameObject);
            LOG_ERROR("[PhysicsWorld] Failed to track a Collider because memory allocation failed.");
            return false;
        }
        catch (const std::length_error&)
        {
            actor->detachShape(*shape);
            shape->release();
            m_impl->removeEmptyStaticActor(gameObject);
            LOG_ERROR("[PhysicsWorld] Collider collection reached its maximum capacity.");
            return false;
        }
        if (const auto body = m_impl->rigidbodies.find(gameObject);
            body != m_impl->rigidbodies.end() && body->second.actor != nullptr
            && !body->second.component->m_isKinematic)
            PxRigidBodyExt::updateMassAndInertia(*body->second.actor, body->second.component->m_mass);
        return true;
    }

    void PhysicsWorld::destroyCollider(ColliderComponent& component) noexcept
    {
        if (m_impl == nullptr || m_impl->simulationPending)
            return;
        const auto found = m_impl->colliders.find(&component);
        if (found == m_impl->colliders.end())
            return;

        GameObject* const gameObject = component.getGameObject();
        m_impl->removeActivePairsForCollider(component);
        if (found->second.actor != nullptr && found->second.shape != nullptr)
            found->second.actor->detachShape(*found->second.shape);
        if (found->second.shape != nullptr)
            found->second.shape->release();
        m_impl->colliders.erase(found);
        if (gameObject != nullptr)
        {
            const auto body = m_impl->rigidbodies.find(gameObject);
            if (body != m_impl->rigidbodies.end() && body->second.actor != nullptr
                && !body->second.component->m_isKinematic
                && body->second.actor->getNbShapes() > 0)
                PxRigidBodyExt::updateMassAndInertia(*body->second.actor, body->second.component->m_mass);
        }
        m_impl->removeEmptyStaticActor(gameObject);
    }

    void PhysicsWorld::updateCollider(ColliderComponent& component) noexcept
    {
        if (m_impl == nullptr || m_impl->simulationPending)
            return;
        const auto found = m_impl->colliders.find(&component);
        if (found != m_impl->colliders.end())
            m_impl->setShapeGeometry(found->second);
    }

    bool PhysicsWorld::createController(CharacterControllerComponent& component) noexcept
    {
        if (!isInitialized() || m_impl->controllerManager == nullptr
            || m_impl->simulationPending || component.getGameObject() == nullptr)
            return false;
        if (m_impl->controllers.contains(&component))
            return true;

        GameObject* const gameObject = component.getGameObject();
        const Vector3 position = gameObject->getWorldPosition();
        PxMaterial* const material = m_impl->physics->createMaterial(0.5f, 0.5f, 0.0f);
        if (material == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] PhysX failed to create a Character Controller material.");
            return false;
        }

        PxCapsuleControllerDesc description;
        description.radius = component.m_radius;
        description.height = component.m_height - (2.0f * component.m_radius);
        description.position = PxExtendedVec3(position.x, position.y, position.z);
        description.upDirection = PxVec3(0.0f, 1.0f, 0.0f);
        description.stepOffset = 0.3f;
        description.material = material;
        if (!description.isValid())
        {
            material->release();
            LOG_ERROR("[PhysicsWorld] Character Controller descriptor is invalid.");
            return false;
        }

        PxController* const baseController =
            m_impl->controllerManager->createController(description);
        material->release();
        if (baseController == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] PhysX failed to create a Character Controller.");
            return false;
        }

        auto* const controller = static_cast<PxCapsuleController*>(baseController);
        controller->setUserData(&component);
        PxRigidDynamic* const actor = controller->getActor();
        actor->userData = &component;
        PxFilterData filterData;
        filterData.word0 = static_cast<PxU32>(component.m_layer);
        filterData.word1 = component.m_collisionMask;
        const PxU32 shapeCount = actor->getNbShapes();
        if (shapeCount != 1)
        {
            controller->release();
            LOG_ERROR("[PhysicsWorld] PhysX Character Controller must have exactly one capsule shape.");
            return false;
        }
        PxShape* shape = nullptr;
        actor->getShapes(&shape, 1);
        if (shape == nullptr)
        {
            controller->release();
            LOG_ERROR("[PhysicsWorld] PhysX Character Controller has no capsule shape.");
            return false;
        }
        shape->setSimulationFilterData(filterData);
        shape->setQueryFilterData(filterData);

        try
        {
            const Transform* const localTransform = gameObject->getTransform();
            if (localTransform == nullptr)
            {
                controller->release();
                LOG_ERROR("[PhysicsWorld] Cannot create a Character Controller without a Transform.");
                return false;
            }
            m_impl->controllers.emplace(&component, Impl::ControllerRecord{
                &component,
                controller,
                position,
                localTransform->getRotation() });
        }
        catch (const std::bad_alloc&)
        {
            controller->release();
            LOG_ERROR("[PhysicsWorld] Failed to track a Character Controller because memory allocation failed.");
            return false;
        }
        catch (const std::length_error&)
        {
            controller->release();
            LOG_ERROR("[PhysicsWorld] Character Controller collection reached its maximum capacity.");
            return false;
        }

        component.m_position = position;
        component.m_isGrounded = false;
        return true;
    }

    void PhysicsWorld::destroyController(CharacterControllerComponent& component) noexcept
    {
        if (m_impl == nullptr || m_impl->simulationPending || m_impl->controllerManager == nullptr)
            return;

        const auto found = m_impl->controllers.find(&component);
        if (found == m_impl->controllers.end())
            return;
        if (found->second.controller != nullptr)
            found->second.controller->release();
        m_impl->controllers.erase(found);
    }

    void PhysicsWorld::updateController(CharacterControllerComponent& component) noexcept
    {
        if (m_impl == nullptr || m_impl->simulationPending)
            return;

        const auto found = m_impl->controllers.find(&component);
        if (found == m_impl->controllers.end() || found->second.controller == nullptr)
            return;

        PxCapsuleController& controller = *found->second.controller;
        if (!controller.setRadius(component.m_radius))
        {
            LOG_ERROR("[PhysicsWorld] PhysX rejected the Character Controller radius.");
            return;
        }
        controller.resize(component.m_height - (2.0f * component.m_radius));

        PxFilterData filterData;
        filterData.word0 = static_cast<PxU32>(component.m_layer);
        filterData.word1 = component.m_collisionMask;
        PxRigidDynamic* const actor = controller.getActor();
        const PxU32 shapeCount = actor->getNbShapes();
        if (shapeCount != 1)
        {
            LOG_ERROR("[PhysicsWorld] PhysX Character Controller must have exactly one capsule shape.");
            return;
        }
        PxShape* shape = nullptr;
        actor->getShapes(&shape, 1);
        if (shape == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] PhysX Character Controller has no capsule shape.");
            return;
        }
        shape->setSimulationFilterData(filterData);
        shape->setQueryFilterData(filterData);
        m_impl->scene->resetFiltering(*actor);
    }

    bool PhysicsWorld::moveController(
        CharacterControllerComponent& component,
        const Vector3& displacement,
        const float deltaTime) noexcept
    {
        if (m_impl == nullptr || m_impl->simulationPending
            || !isFiniteVector(displacement) || !std::isfinite(deltaTime) || deltaTime <= 0.0f)
            return false;

        const auto found = m_impl->controllers.find(&component);
        if (found == m_impl->controllers.end() || found->second.controller == nullptr)
            return false;

        CharacterShapeFilterCallback shapeFilter(component.m_layer, component.m_collisionMask);
        CharacterControllerFilterCallback controllerFilter;
        PxFilterData filterData;
        filterData.word0 = static_cast<PxU32>(component.m_layer);
        filterData.word1 = component.m_collisionMask;
        const PxControllerFilters filters(&filterData, &shapeFilter, &controllerFilter);
        const PxControllerCollisionFlags collisionFlags = found->second.controller->move(
            PhysicsConversions::toPx(displacement),
            0.001f,
            deltaTime,
            filters);

        const PxExtendedVec3 footPosition = found->second.controller->getFootPosition();
        const Vector3 worldPosition(
            static_cast<float>(footPosition.x),
            static_cast<float>(footPosition.y),
            static_cast<float>(footPosition.z));
        if (!isFiniteVector(worldPosition))
        {
            LOG_ERROR("[PhysicsWorld] Character Controller returned a non-finite position.");
            return false;
        }

        component.m_position = worldPosition;
        component.m_isGrounded = collisionFlags.isSet(PxControllerCollisionFlag::eCOLLISION_DOWN);

        GameObject* const gameObject = component.getGameObject();
        TransformComponent* const transformComponent =
            gameObject != nullptr ? gameObject->getComponent<TransformComponent>() : nullptr;
        if (transformComponent == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] Character Controller GameObject has no TransformComponent.");
            return false;
        }

        Vector3 localPosition = worldPosition;
        if (const GameObject* const parent = gameObject->getParent())
        {
            localPosition = Vector3::Transform(
                worldPosition,
                parent->getWorldTransform().toInverseMatrix());
        }
        transformComponent->localTransform().setPosition(localPosition);
        return true;
    }

    bool PhysicsWorld::createJoint(JointComponent& component) noexcept
    {
        if (!isInitialized() || m_impl->simulationPending || component.getGameObject() == nullptr)
            return false;
        if (m_impl->joints.contains(&component))
            return true;

        RigidbodyComponent* const bodyA =
            component.getGameObject()->getComponent<RigidbodyComponent>();
        if (bodyA == nullptr)
        {
            LOG_WARNING("[PhysicsWorld] Joint is waiting for a Rigidbody on its GameObject.");
        }

        try
        {
            m_impl->joints.emplace(&component, Impl::JointRecord{
                &component,
                bodyA,
                component.m_connectedBody,
                nullptr });
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsWorld] Failed to track a Joint because memory allocation failed.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[PhysicsWorld] Joint collection reached its maximum capacity.");
            return false;
        }

        updateJoint(component);
        return true;
    }

    void PhysicsWorld::destroyJoint(JointComponent& component) noexcept
    {
        if (m_impl == nullptr || m_impl->simulationPending)
            return;

        const auto found = m_impl->joints.find(&component);
        if (found == m_impl->joints.end())
            return;
        if (found->second.joint != nullptr)
            found->second.joint->release();
        m_impl->joints.erase(found);
    }

    void PhysicsWorld::updateJoint(JointComponent& component) noexcept
    {
        if (m_impl == nullptr || m_impl->simulationPending)
            return;

        const auto found = m_impl->joints.find(&component);
        if (found == m_impl->joints.end())
            return;

        Impl::JointRecord& record = found->second;
        if (record.joint != nullptr)
        {
            record.joint->release();
            record.joint = nullptr;
        }
        record.bodyA = component.getGameObject() != nullptr
            ? component.getGameObject()->getComponent<RigidbodyComponent>()
            : nullptr;
        record.bodyB = component.m_connectedBody;

        PxRigidDynamic* const actorA = m_impl->findRigidbodyActor(record.bodyA);
        if (actorA == nullptr)
            return;

        PxRigidDynamic* const actorB = m_impl->findRigidbodyActor(record.bodyB);
        if (record.bodyB != nullptr && actorB == nullptr)
            return;
        if (actorA == actorB)
        {
            LOG_ERROR("[PhysicsWorld] A Joint cannot connect a Rigidbody to itself.");
            return;
        }

        const PxTransform localFrame0(
            PhysicsConversions::toPx(component.m_localAnchor),
            PxQuat(PxIdentity));
        const PxTransform localFrame1 = actorB != nullptr
            ? PxTransform(PhysicsConversions::toPx(component.m_connectedAnchor), PxQuat(PxIdentity))
            : actorA->getGlobalPose() * localFrame0;

        PxJoint* joint = nullptr;
        switch (component.m_type)
        {
        case PhysicsJointType::Fixed:
            joint = PxFixedJointCreate(
                *m_impl->physics,
                actorA,
                localFrame0,
                actorB,
                localFrame1);
            break;
        case PhysicsJointType::Revolute:
        {
            PxRevoluteJoint* const revolute = PxRevoluteJointCreate(
                *m_impl->physics,
                actorA,
                localFrame0,
                actorB,
                localFrame1);
            if (revolute != nullptr && component.m_revoluteLimitSet)
            {
                revolute->setLimit(PxJointAngularLimitPair(
                    component.m_revoluteMinimum,
                    component.m_revoluteMaximum));
                revolute->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, true);
            }
            joint = revolute;
            break;
        }
        case PhysicsJointType::Distance:
        {
            PxDistanceJoint* const distance = PxDistanceJointCreate(
                *m_impl->physics,
                actorA,
                localFrame0,
                actorB,
                localFrame1);
            if (distance != nullptr)
            {
                const PxVec3 worldAnchor0 = actorA->getGlobalPose().transform(localFrame0.p);
                const PxVec3 worldAnchor1 = actorB != nullptr
                    ? actorB->getGlobalPose().transform(localFrame1.p)
                    : localFrame1.p;
                const float initialDistance = (worldAnchor1 - worldAnchor0).magnitude();
                distance->setTolerance(0.025f);
                distance->setMinDistance(component.m_distanceRangeSet
                    ? component.m_distanceMinimum
                    : 0.0f);
                distance->setMaxDistance(component.m_distanceRangeSet
                    ? component.m_distanceMaximum
                    : initialDistance);
                distance->setDistanceJointFlag(
                    PxDistanceJointFlag::eMIN_DISTANCE_ENABLED,
                    component.m_distanceRangeSet && component.m_distanceMinimum > 0.0f);
                distance->setDistanceJointFlag(
                    PxDistanceJointFlag::eMAX_DISTANCE_ENABLED,
                    true);
            }
            joint = distance;
            break;
        }
        case PhysicsJointType::D6:
        {
            PxD6Joint* const d6 = PxD6JointCreate(
                *m_impl->physics,
                actorA,
                localFrame0,
                actorB,
                localFrame1);
            if (d6 != nullptr)
            {
                for (std::size_t axisIndex = 0; axisIndex < component.m_d6FreeAxes.size(); ++axisIndex)
                {
                    const auto axis = static_cast<PxD6Axis::Enum>(axisIndex);
                    d6->setMotion(
                        axis,
                        component.m_d6FreeAxes[axisIndex]
                            ? PxD6Motion::eFREE
                            : PxD6Motion::eLOCKED);
                }
            }
            joint = d6;
            break;
        }
        }

        if (joint == nullptr)
        {
            LOG_ERROR("[PhysicsWorld] PhysX failed to create the requested Joint.");
            return;
        }

        joint->setBreakForce(component.m_breakForce, component.m_breakTorque);
        joint->setConstraintFlag(
            PxConstraintFlag::eCOLLISION_ENABLED,
            component.m_collisionEnabled);
        record.joint = joint;
    }
} // namespace Engine
