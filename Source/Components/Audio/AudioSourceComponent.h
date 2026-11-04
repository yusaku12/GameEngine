#pragma once

#include <span>
#include "Audio\AudioSystem.h"
#include "Core\GameObject\Component.h"

namespace Engine
{
    /** @brief Scene-persisted WAV playback and spatial attenuation settings. */
    struct AudioSourceSettings
    {
        std::filesystem::path path;
        float volume = 1;
        float pitch = 1;
        bool loop = false;
        bool playOnAwake = true;
        bool spatial = false;
        float minDistance = 1;
        float maxDistance = 50;
    };
    /**
     * @brief A GameObject-owned sound emitter; Stop never modifies editing voices.
     * @thread_safety Not thread-safe. Use on the GameUpdate thread or between joined frames.
     */
    class AudioSourceComponent final : public Component
    {
    public:
        ~AudioSourceComponent() override;
        /** @brief Returns current playback configuration. */
        const AudioSourceSettings& settings() const noexcept { return m_settings; }
        /** @brief Validates settings, stopping a voice if its clip/loop/spatial mode changes. */
        bool setSettings(const AudioSourceSettings& settings);
        /** @brief Assigns a valid in-memory cached WAV clip. */
        bool setClip(AudioClipHandle clip);
        /** @brief Returns the current cached resource handle. */
        AudioClipHandle clip() const noexcept { return m_clip; }
        /** @brief Starts/restarts playback in an active game session, logging failures. */
        bool play();
        /** @brief Pauses/resumes this emitter without resetting playback. */
        void pause(bool paused = true) noexcept;
        /** @brief Stops and releases this emitter's playback voice. */
        void stop() noexcept;
        /** @brief True while audio remains queued, even when individually paused. */
        bool isPlaying() const noexcept;
        /** @brief Returns the versioned playback configuration format. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Persists playback settings and a UTF-8 resource path. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Restores validated configuration without starting playback. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
        /** @brief Shares only the immutable clip handle, not the playback voice. */
        bool copyConfigurationTo(Component& destination) const override;
    protected:
        void onEnable() override;
        void onDisable() override;
        void onDestroy() override;
        void onImGui() override;
    private:
        AudioSourceSettings m_settings;
        AudioClipHandle m_clip;
    };

    /**
     * @brief Marks the active 3D listener; local +Z/+Y define forward/up.
     * @details Exactly one enabled listener is expected across active/persistent scenes.
     * @thread_safety Access follows the engine frame barrier.
     */
    class AudioListenerComponent final : public Component
    {
    public:
        /** @brief Returns the versioned listener marker format. */
        std::uint32_t getPayloadVersion() const noexcept override { return 1; }
        /** @brief Persists the listener marker. */
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        /** @brief Validates a listener payload. */
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;
    };
}
