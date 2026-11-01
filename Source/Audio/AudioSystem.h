#pragma once
#include <memory>
#include <filesystem>
#include <cstdint>

namespace Engine
{
    class AudioSourceComponent;
    class Scene;
    /** @brief Generation-checked handle to an immutable WAV resource. */
    struct AudioClipHandle
    {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
        /** @brief True for a nonzero handle generation; use isClipValid to check liveness. */
        bool isValid() const noexcept { return generation != 0; }
        bool operator==(const AudioClipHandle&) const = default;
    };
    /**
     * @brief Owns XAudio2, immutable WAV resources and source voices through RAII.
     * @thread_safety Not thread-safe. Access only at the joined engine frame barrier.
     */
    class AudioSystem final
    {
    public:
        /** @brief Returns the process-wide audio service. */
        static AudioSystem& instance() noexcept;
        ~AudioSystem();
        /** @brief Opens the default output device; failures are logged. */
        bool initialize();
        /** @brief Releases voices before device and resource shutdown. */
        void shutdown() noexcept;
        /** @brief Loads PCM/IEEE-float RIFF WAV data into the immutable resource cache. */
        AudioClipHandle load(const std::filesystem::path& path);
        /** @brief Validates a clip handle against its cached generation. */
        bool isClipValid(AudioClipHandle handle) const noexcept;
        /** @brief Returns a valid clip's path, or an empty path for an invalid handle. */
        std::filesystem::path path(AudioClipHandle handle) const;
        /** @brief Opens output on demand and creates/restarts a voice; spatial clips must be mono. */
        bool play(AudioSourceComponent& source);
        /** @brief Destroys a component's voice, releasing its resource reference. */
        void stop(AudioSourceComponent& source) noexcept;
        /** @brief Pauses/resumes one voice without discarding its cursor. */
        void pause(AudioSourceComponent& source, bool paused) noexcept;
        /** @brief True while a source has queued audio buffers. */
        bool isPlaying(const AudioSourceComponent& source) const noexcept;
        /** @brief Returns consumed sample time including loops; zero if no voice exists. */
        double playbackSeconds(const AudioSourceComponent& source) const noexcept;
        /** @brief Freezes/resumes all mixing, preserving voice cursors. */
        bool setPaused(bool paused) noexcept;
        /** @brief Removes every gameplay voice, including individually paused voices. */
        void stopAll() noexcept;
        /** @brief Applies source volume/pitch and listener-relative 3D matrix/doppler. */
        void update(const Scene* active, const Scene& persistent, float deltaTime) noexcept;
    private:
        AudioSystem();
        AudioSystem(const AudioSystem&) = delete;
        AudioSystem& operator=(const AudioSystem&) = delete;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
