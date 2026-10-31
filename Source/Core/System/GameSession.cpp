#include "Pch.h"
#include "Core\System\GameSession.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneManager.h"
#include "Core\GameObject\Component\CameraComponent.h"
#include "Editor\Camera\FreeCameraController.h"
#include "Graphics\Camera\CameraManager.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Physics\PhysicsSystem.h"

namespace Engine
{
    GameSession& GameSession::instance() noexcept
    {
        static GameSession session;
        return session;
    }

    GameSession::~GameSession() = default;

    void GameSession::initialize(const ExecutionMode mode) noexcept
    {
        if (m_savedPersistent != nullptr)
        {
            LOG_ERROR("[Play] Cannot initialize execution mode during an isolated Play session.");
            return;
        }
        m_mode = mode;
        m_state = mode == ExecutionMode::Runtime ? PlayState::Playing : PlayState::Editing;
        m_stepRequested = false;
        m_stepThisFrame = false;
        m_gameViewFocused = false;
        m_status.clear();
    }

    bool GameSession::play()
    {
        SceneManager& manager = SceneManager::instance();
        if (m_mode != ExecutionMode::Editor || isPlaying() || manager.getActiveScene() == nullptr || manager.isBusy())
        {
            m_status = "Play requires an active editing scene.";
            LOG_ERROR("[Play] {}", m_status);
            return false;
        }
        try
        {
            manager.processDestroyQueue();
            std::vector<const Scene*> sources;
            manager.forEachScene([&](const Scene& scene)
            {
                if (&scene != manager.getPersistentScene())
                    sources.push_back(&scene);
            });
            std::vector<std::unique_ptr<Scene>> clones;
            clones.reserve(sources.size());
            auto persistent = std::make_unique<Scene>(ObjectGUID::generate(), "Persistent");
            std::vector<SceneClonePair> pairs;
            pairs.reserve(sources.size() + 1);
            Scene* active = nullptr;
            for (const Scene* source : sources)
            {
                auto clone = std::make_unique<Scene>(ObjectGUID::generate(), source->getName());
                if (source == manager.getActiveScene())
                    active = clone.get();
                pairs.push_back({ *source, *clone });
                clones.push_back(std::move(clone));
            }
            pairs.push_back({ *manager.getPersistentScene(), *persistent });
            if (!cloneScenes(pairs))
            {
                m_status = "Play failed to clone scene configuration; see the engine log.";
                return false;
            }

            CameraComponent* mainCamera = CameraManager::instance().getMainCamera();
            m_savedMainCamera = mainCamera != nullptr ? mainCamera->getGameObject()->getGUID() : ObjectGUID{};
            if (!manager.getPersistentScene()->exchangeGameObjects(*persistent))
                return false;
            if (!manager.exchangeScenes(clones, active))
            {
                manager.getPersistentScene()->exchangeGameObjects(*persistent);
                return false;
            }
            m_savedScenes = std::move(clones);
            m_savedActive = active;
            m_savedPersistent = std::move(persistent);
            TimeManager& time = TimeManager::instance();
            m_savedTimeScale = time.timeScale();
            m_savedFixedDeltaTime = time.fixedDeltaTime();
            m_savedMaxDeltaTime = time.maxDeltaTime();
            m_savedTimePaused = time.isPaused();
            m_savedPhysicsPaused = PhysicsSystem::instance().isPaused();
            time.setPaused(false);
            time.setTimeScale(1.0f);
            time.resetSimulation();
            PhysicsSystem::instance().setPaused(false);
            CameraManager::instance().shutdown();
            manager.forEachScene([this](Scene& scene)
            {
                if (GameObject* object = scene.find(m_savedMainCamera))
                {
                    if (auto* camera = object->getComponent<CameraComponent>())
                    {
                        CameraManager::instance().registerCamera(camera);
                        CameraManager::instance().setMainCamera(camera);
                    }
                }
            });
            ModelRenderSubmissionQueue::instance().clear();
            CameraRenderSubmissionQueue::instance().clear();
            InputManager::instance().resetState();
            m_state = PlayState::Playing;
            m_stepRequested = false;
            m_status.clear();
            LOG_INFO("[Play] Started isolated game session.");
            return true;
        }
        catch (const std::bad_alloc&)
        {
            m_status = "Play failed: insufficient memory.";
        }
        catch (const std::length_error&)
        {
            m_status = "Play failed: scene collection exceeded capacity.";
        }
        LOG_ERROR("[Play] {}", m_status);
        return false;
    }

    bool GameSession::stop() noexcept
    {
        if (m_mode != ExecutionMode::Editor || !isPlaying() || m_savedPersistent == nullptr
            || SceneManager::instance().isBusy())
        {
            LOG_ERROR("[Play] Stop requires an active editor Play session.");
            return false;
        }
        SceneManager& manager = SceneManager::instance();
        if (!manager.discardScenes())
            return false;
        if (!manager.exchangeScenes(m_savedScenes, m_savedActive))
            return false;
        if (!manager.getPersistentScene()->exchangeGameObjects(*m_savedPersistent))
            return false;
        m_savedScenes.clear();
        m_savedPersistent.reset();
        m_savedActive = nullptr;
        CameraManager::instance().shutdown();
        manager.forEachScene([this](Scene& scene)
        {
            if (GameObject* object = scene.find(m_savedMainCamera))
            {
                if (auto* camera = object->getComponent<CameraComponent>())
                {
                    CameraManager::instance().registerCamera(camera);
                    CameraManager::instance().setMainCamera(camera);
                }
            }
        });
        ModelRenderSubmissionQueue::instance().clear();
        CameraRenderSubmissionQueue::instance().clear();
        TimeManager& time = TimeManager::instance();
        time.resetSimulation();
        time.setTimeScale(m_savedTimeScale);
        time.setFixedDeltaTime(m_savedFixedDeltaTime);
        time.setMaxDeltaTime(m_savedMaxDeltaTime);
        time.setPaused(m_savedTimePaused);
        PhysicsSystem::instance().setPaused(m_savedPhysicsPaused);
        InputManager::instance().resetState();
        m_state = PlayState::Editing;
        m_stepRequested = false;
        m_stepThisFrame = false;
        m_status.clear();
        LOG_INFO("[Play] Stopped; restored editor scene objects.");
        return true;
    }

    bool GameSession::setPaused(const bool paused) noexcept
    {
        if (m_mode != ExecutionMode::Editor || !isPlaying())
        {
            LOG_ERROR("[Play] Pause requires an active editor Play session.");
            return false;
        }
        m_state = paused ? PlayState::Paused : PlayState::Playing;
        m_stepRequested = false;
        InputManager::instance().resetState();
        return true;
    }

    bool GameSession::step() noexcept
    {
        if (m_state != PlayState::Paused || m_stepRequested)
        {
            LOG_ERROR("[Play] Step requires a paused session without a pending step.");
            return false;
        }
        m_stepRequested = true;
        return true;
    }

    void GameSession::beginFrame(const bool windowFocused)
    {
        m_stepThisFrame = std::exchange(m_stepRequested, false);
        TimeManager::instance().beginFrame(m_state == PlayState::Playing, m_stepThisFrame);
        InputManager& input = InputManager::instance();
        const bool acceptsInput = windowFocused
            && (m_mode == ExecutionMode::Runtime || m_gameViewFocused)
            && (m_state != PlayState::Paused || m_stepThisFrame);
        input.setWindowFocused(acceptsInput);
        input.update();
    }

    void GameSession::update()
    {
        SceneManager& manager = SceneManager::instance();
        TimeManager& time = TimeManager::instance();
        if (m_state == PlayState::Playing || m_stepThisFrame)
        {
            manager.update(time.deltaTime());
            while (time.hasFixedUpdate())
            {
                manager.fixedUpdate(time.fixedDeltaTime());
                if (m_stepThisFrame)
                    PhysicsSystem::instance().update(time.fixedDeltaTime());
                else if (PhysicsSystem::instance().isPaused())
                    PhysicsSystem::instance().updateFrame(time.fixedDeltaTime());
                else
                    PhysicsSystem::instance().update(time.fixedDeltaTime());
                time.consumeFixedUpdate();
            }
            manager.lateUpdate(time.deltaTime());
            manager.processDestroyQueue();
        }
        else
        {
            if (m_state == PlayState::Editing && m_gameViewFocused)
            {
                if (CameraComponent* camera = CameraManager::instance().getActiveCamera())
                {
                    if (auto* controller = camera->getGameObject()->getComponent<FreeCameraController>();
                        controller != nullptr && controller->isEnabled())
                        controller->updateEditor(time.unscaledDeltaTime());
                }
            }
            CameraManager::instance().shutdown();
            if (Scene* scene = manager.getActiveScene())
                scene->submitRenderData();
            manager.getPersistentScene()->submitRenderData();
            manager.processDestroyQueue();
        }
        if (m_mode == ExecutionMode::Editor)
            PhysicsSystem::instance().drawDebug();
    }

    void GameSession::shutdown() noexcept
    {
        if (m_mode == ExecutionMode::Editor && isPlaying())
            static_cast<void>(stop());
    }
}
