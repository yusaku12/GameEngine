#pragma once

#include <memory>
#include <string>
#include <vector>
#include "Core\Object\ObjectGUID.h"
#include "Core\System\ExecutionMode.h"

namespace Engine
{
    class Scene;

    /** @brief State of the editor's isolated game session. */
    enum class PlayState { Editing, Playing, Paused };

    /**
     * @brief Controls simulation and preserves editor scene ownership across Play/Stop.
     * @details Transitions execute on the main thread between joined update/render jobs.
     * Read-only access by those jobs is safe only while the main thread waits for them.
     * @thread_safety Not thread-safe. External access must follow the frame barrier.
     */
    class GameSession final
    {
    public:
        /** @brief Returns the process-wide execution session. */
        static GameSession& instance() noexcept;
        ~GameSession();

        /** @brief Configures startup mode before the first frame. */
        void initialize(ExecutionMode mode) noexcept;
        /** @brief Clones managed and persistent scenes and starts isolated gameplay. */
        bool play();
        /** @brief Stops gameplay and restores the original editor objects. */
        bool stop() noexcept;
        /** @brief Pauses/resumes an active editor Play session. */
        bool setPaused(bool paused) noexcept;
        /** @brief Requests exactly one fixed-duration game frame while paused. */
        bool step() noexcept;
        /** @brief Prepares time and input at the main-thread frame boundary. */
        void beginFrame(bool windowFocused);
        /** @brief Runs gameplay or submits unchanged scene data while editing/paused. */
        void update();
        /** @brief Releases a Play session before SceneManager shutdown. */
        void shutdown() noexcept;
        /** @brief Returns editor or Runtime presentation mode. */
        ExecutionMode mode() const noexcept { return m_mode; }
        /** @brief Returns current Play state. */
        PlayState state() const noexcept { return m_state; }
        /** @brief True while gameplay owns isolated scene data, including Pause. */
        bool isPlaying() const noexcept { return m_state != PlayState::Editing; }
        /** @brief Sets whether the editor Game view owns gameplay input. */
        void setGameViewFocused(bool focused) noexcept { m_gameViewFocused = focused; }
        /** @brief Returns the most recent transition error/status for editor display. */
        const std::string& status() const noexcept { return m_status; }

    private:
        GameSession() = default;
        GameSession(const GameSession&) = delete;
        GameSession& operator=(const GameSession&) = delete;

        ExecutionMode m_mode = ExecutionMode::Editor;
        PlayState m_state = PlayState::Editing;
        std::vector<std::unique_ptr<Scene>> m_savedScenes;
        std::unique_ptr<Scene> m_savedPersistent;
        Scene* m_savedActive = nullptr;
        ObjectGUID m_savedMainCamera;
        float m_savedTimeScale = 1.0f;
        float m_savedFixedDeltaTime = 1.0f / 60.0f;
        float m_savedMaxDeltaTime = 0.1f;
        bool m_savedTimePaused = false;
        bool m_savedPhysicsPaused = false;
        bool m_stepRequested = false;
        bool m_stepThisFrame = false;
        bool m_gameViewFocused = false;
        std::string m_status;
    };
}
