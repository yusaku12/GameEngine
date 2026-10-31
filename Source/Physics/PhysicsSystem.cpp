#include "Pch.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Physics\Character\CharacterControllerComponent.h"
#include "Physics\Joint\JointComponent.h"
#include "Physics\Collider\BoxColliderComponent.h"
#include "Physics\Collider\CapsuleColliderComponent.h"
#include "Physics\Collider\ConvexColliderComponent.h"
#include "Physics\Collider\MeshColliderComponent.h"
#include "Physics\Collider\SphereColliderComponent.h"
#include "Physics\PhysicsSystem.h"
#include "Physics\PhysicsWorld.h"
#include "Physics\Rigidbody\RigidbodyComponent.h"
#include <chrono>

namespace Engine
{
    PhysicsSystem& PhysicsSystem::instance() noexcept
    {
        static PhysicsSystem system;
        return system;
    }

    PhysicsSystem::~PhysicsSystem()
    {
        shutdown();
    }

    bool PhysicsSystem::initialize() noexcept
    {
        if (isInitialized())
            return true;

        {
            const std::lock_guard lock(m_debugMutex);
            m_profilerSnapshot = {};
            m_stepDurations.fill(0.0);
            m_stepDurationCursor = 0;
            m_stepDurationCount = 0;
            m_paused = false;
            m_stepRequested = false;
        }

        ComponentRegistry& registry = ComponentRegistry::instance();
        if (registry.registerType<RigidbodyComponent>("Rigidbody", false, false, true,
                [](const GameObject& object) -> std::string_view
                {
                    return object.hasComponent<MeshColliderComponent>()
                        ? "Mesh Collider requires a static actor without a Rigidbody." : std::string_view{};
                }) == 0
            || registry.registerType<BoxColliderComponent>("BoxCollider", false, false, true) == 0
            || registry.registerType<SphereColliderComponent>("SphereCollider", false, false, true) == 0
            || registry.registerType<CapsuleColliderComponent>("CapsuleCollider", false, false, true) == 0
            || registry.registerType<MeshColliderComponent>("MeshCollider", false, false, true,
                [](const GameObject& object) -> std::string_view
                {
                    return object.hasComponent<RigidbodyComponent>()
                        ? "Mesh Collider cannot be attached to a Rigidbody." : std::string_view{};
                }) == 0
            || registry.registerType<ConvexColliderComponent>("ConvexCollider", false, false, true) == 0
            || registry.registerType<CharacterControllerComponent>("CharacterController", false, false, true) == 0
            || registry.registerType<JointComponent>("Joint", false, false, true,
                [](const GameObject& object) -> std::string_view
                {
                    return object.hasComponent<RigidbodyComponent>()
                        ? std::string_view{} : "Add a Rigidbody before adding a Joint.";
                }) == 0)
        {
            LOG_ERROR("[PhysicsSystem] Failed to register Physics components.");
            return false;
        }

        try
        {
            auto world = std::make_unique<PhysicsWorld>();
            if (!world->initialize())
                return false;
            m_world = std::move(world);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsSystem] Failed to allocate PhysicsWorld.");
            return false;
        }

        LOG_INFO("[PhysicsSystem] PhysX initialized.");
        return true;
    }

    void PhysicsSystem::shutdown() noexcept
    {
        if (m_world == nullptr)
            return;

        m_world->shutdown();
        m_world.reset();
        LOG_INFO("[PhysicsSystem] PhysX shut down.");
    }

    void PhysicsSystem::update(const float fixedDeltaTime) noexcept
    {
        FrameProfiler::ScopedCpuTimer profile(FrameProfileMetric::Physics);
        if (m_world == nullptr || !m_world->isInitialized())
        {
            LOG_ERROR("[PhysicsSystem] Cannot update because Physics is not initialized.");
            return;
        }
        if (!std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f)
        {
            LOG_ERROR("[PhysicsSystem] Fixed timestep must be a positive finite value.");
            return;
        }

        const PhysicsDebugSettings debugSettings = getDebugSettings();
        m_world->setDebugCapture(
            debugSettings.enabled && debugSettings.drawContacts,
            debugSettings.enabled && debugSettings.drawRaycasts);
        const auto start = std::chrono::steady_clock::now();
        m_world->simulate(fixedDeltaTime);
        m_world->fetchResults();
        const auto end = std::chrono::steady_clock::now();
        const double stepMilliseconds = std::chrono::duration<double, std::milli>(end - start).count();
        const PhysicsWorldStatistics statistics = m_world->getStatistics();
        {
            const std::lock_guard lock(m_debugMutex);
            m_profilerSnapshot.fixedDeltaTime = fixedDeltaTime;
            m_profilerSnapshot.lastStepMilliseconds = stepMilliseconds;
            ++m_profilerSnapshot.totalSteps;
            m_stepDurations[m_stepDurationCursor] = stepMilliseconds;
            m_stepDurationCursor = (m_stepDurationCursor + 1) % m_stepDurations.size();
            m_stepDurationCount = std::min(m_stepDurationCount + 1, m_stepDurations.size());
            double totalStepMilliseconds = 0.0;
            for (std::size_t index = 0; index < m_stepDurationCount; ++index)
                totalStepMilliseconds += m_stepDurations[index];
            m_profilerSnapshot.averageStepMilliseconds =
                totalStepMilliseconds / static_cast<double>(m_stepDurationCount);
            m_profilerSnapshot.activeBodies = statistics.activeBodies;
            m_profilerSnapshot.activeActors = statistics.activeActors;
            m_profilerSnapshot.dynamicBodies = statistics.dynamicBodies;
            m_profilerSnapshot.staticBodies = statistics.staticBodies;
            m_profilerSnapshot.activeShapes = statistics.activeShapes;
            m_profilerSnapshot.activeJoints = statistics.activeJoints;
            m_profilerSnapshot.activeContacts = statistics.activeContacts;
        }
        dispatchEvents();
    }

    void PhysicsSystem::dispatchEvents() noexcept
    {
        if (m_world != nullptr && m_world->isInitialized())
            m_world->dispatchEvents();
    }

    void PhysicsSystem::drawDebug() noexcept
    {
        if (m_world == nullptr || !m_world->isInitialized())
            return;
        m_world->drawDebug(getDebugSettings());
    }

    void PhysicsSystem::setPaused(const bool paused) noexcept
    {
        const std::lock_guard lock(m_debugMutex);
        m_paused = paused;
        if (!paused)
            m_stepRequested = false;
    }

    bool PhysicsSystem::isPaused() const noexcept
    {
        const std::lock_guard lock(m_debugMutex);
        return m_paused;
    }

    bool PhysicsSystem::canAdvancePhysics() const noexcept
    {
        const std::lock_guard lock(m_debugMutex);
        return !m_paused || m_stepRequested;
    }

    bool PhysicsSystem::requestSingleStep() noexcept
    {
        const std::lock_guard lock(m_debugMutex);
        if (!m_paused || m_stepRequested)
            return false;
        m_stepRequested = true;
        return true;
    }

    void PhysicsSystem::updateFrame(const float fixedDeltaTime) noexcept
    {
        bool shouldStep = false;
        {
            const std::lock_guard lock(m_debugMutex);
            if (m_paused && m_stepRequested)
            {
                m_stepRequested = false;
                shouldStep = true;
            }
        }
        if (shouldStep)
            update(fixedDeltaTime);
    }

    bool PhysicsSystem::reset() noexcept
    {
        if (m_world == nullptr || !m_world->isInitialized())
        {
            LOG_ERROR("[PhysicsSystem] Cannot reset before initialization.");
            return false;
        }
        return m_world->reset();
    }

    bool PhysicsSystem::isInitialized() const noexcept
    {
        return m_world != nullptr && m_world->isInitialized();
    }

    PhysicsDebugSettings PhysicsSystem::getDebugSettings() const noexcept
    {
        const std::lock_guard lock(m_debugMutex);
        return m_debugSettings;
    }

    void PhysicsSystem::setDebugSettings(const PhysicsDebugSettings& settings) noexcept
    {
        const std::lock_guard lock(m_debugMutex);
        m_debugSettings = settings;
    }

    PhysicsProfilerSnapshot PhysicsSystem::getProfilerSnapshot() const noexcept
    {
        const std::lock_guard lock(m_debugMutex);
        return m_profilerSnapshot;
    }
} // namespace Engine
