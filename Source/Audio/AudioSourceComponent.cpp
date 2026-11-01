#include "Pch.h"
#include "Audio\AudioSourceComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"
#include "Core\System\GameSession.h"
#include "Generated\FlatBuffers\AudioComponent_generated.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        const auto sourceType = ComponentRegistry::instance().registerType<AudioSourceComponent>("Audio Source", false, false, true);
        const auto listenerType = ComponentRegistry::instance().registerType<AudioListenerComponent>("Audio Listener", false, false, false);
        const Serialization::AudioComponentPayload* readAudio(const std::uint32_t version,
            const std::span<const std::uint8_t> payload)
        {
            flatbuffers::Verifier verifier(payload.data(), payload.size());
            if (version != 1 || payload.size() < 8 || !Serialization::VerifyAudioComponentPayloadBuffer(verifier))
            {
                LOG_ERROR("[Audio] Invalid component payload.");
                return nullptr;
            }
            return Serialization::GetAudioComponentPayload(payload.data());
        }
    }

    AudioSourceComponent::~AudioSourceComponent() { stop(); }
    bool AudioSourceComponent::setSettings(const AudioSourceSettings& value)
    {
        const auto& path = value.path.native();
        if (path.size() > 32767 || path.find(L'\0') != std::wstring::npos
            || (!path.empty() && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr) == 0)
            || !std::isfinite(value.volume) || value.volume < 0 || value.volume > 4
            || !std::isfinite(value.pitch) || value.pitch < 0.25f || value.pitch > 4
            || !std::isfinite(value.minDistance) || !std::isfinite(value.maxDistance)
            || value.minDistance <= 0 || value.maxDistance <= value.minDistance || value.maxDistance > 100000)
        {
            LOG_ERROR("[Audio] Invalid playback configuration.");
            return false;
        }
        if (value.path != m_settings.path || value.loop != m_settings.loop || value.spatial != m_settings.spatial)
            stop();
        if (value.path != m_settings.path)
            m_clip = {};
        m_settings = value;
        return true;
    }
    bool AudioSourceComponent::setClip(const AudioClipHandle clip)
    {
        const auto path = AudioSystem::instance().path(clip);
        if (path.empty())
        {
            LOG_ERROR("[Audio] Cannot assign an invalid clip handle.");
            return false;
        }
        stop();
        m_settings.path = path;
        m_clip = clip;
        return true;
    }
    bool AudioSourceComponent::play()
    {
        if (!GameSession::instance().isPlaying() || !isEnabled()
            || !getGameObject() || !getGameObject()->isActiveInHierarchy())
        {
            LOG_ERROR("[Audio] Playback requires an enabled source in a running game session.");
            return false;
        }
        if (!AudioSystem::instance().isClipValid(m_clip))
            m_clip = AudioSystem::instance().load(m_settings.path);
        return m_clip.isValid() && AudioSystem::instance().play(*this);
    }
    void AudioSourceComponent::pause(const bool paused) noexcept { AudioSystem::instance().pause(*this, paused); }
    void AudioSourceComponent::stop() noexcept { AudioSystem::instance().stop(*this); }
    bool AudioSourceComponent::isPlaying() const noexcept { return AudioSystem::instance().isPlaying(*this); }
    void AudioSourceComponent::onEnable() { if (m_settings.playOnAwake && !m_settings.path.empty()) play(); }
    void AudioSourceComponent::onDisable() { stop(); }
    void AudioSourceComponent::onDestroy() { stop(); }

    bool AudioSourceComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        const auto path = String::fromWide(m_settings.path.c_str());
        const auto root = Serialization::CreateAudioComponentPayload(builder, builder.CreateString(path.view()),
            m_settings.volume, m_settings.pitch, m_settings.loop, m_settings.playOnAwake, m_settings.spatial,
            m_settings.minDistance, m_settings.maxDistance, false);
        Serialization::FinishAudioComponentPayloadBuffer(builder, root);
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }
    bool AudioSourceComponent::deserializePayload(const std::uint32_t version, const std::span<const std::uint8_t> payload)
    {
        const auto* data = readAudio(version, payload);
        if (!data) return false;
        if (data->listener())
        {
            LOG_ERROR("[Audio] Listener payload cannot be restored as a source.");
            return false;
        }
        AudioSourceSettings value;
        if (data->path())
        {
            const auto path = data->path()->string_view();
            if (path.size() > 65535 || path.find('\0') != std::string_view::npos
                || (!path.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    path.data(), static_cast<int>(path.size()), nullptr, 0) == 0))
            {
                LOG_ERROR("[Audio] WAV path is not valid UTF-8.");
                return false;
            }
            value.path = String(path).toWide();
        }
        value.volume = data->volume();
        value.pitch = data->pitch();
        value.loop = data->loop();
        value.playOnAwake = data->play_on_awake();
        value.spatial = data->spatial();
        value.minDistance = data->min_distance();
        value.maxDistance = data->max_distance();
        return setSettings(value);
    }
    bool AudioSourceComponent::copyConfigurationTo(Component& destination) const
    {
        auto* target = dynamic_cast<AudioSourceComponent*>(&destination);
        if (!target)
        {
            LOG_ERROR("[Audio] Source clone type mismatch.");
            return false;
        }
        target->stop();
        target->m_settings = m_settings;
        target->m_clip = m_clip;
        return true;
    }
    void AudioSourceComponent::onImGui()
    {
        AudioSourceSettings value = m_settings;
        const auto path = String::fromWide(value.path.c_str());
        std::array<char, 2048> buffer{};
        std::copy_n(path.data(), std::min(path.size(), buffer.size() - 1), buffer.data());
        bool changed = ImGui::InputText("WAV Path", buffer.data(), buffer.size());
        if (changed) value.path = String(buffer.data()).toWide();
        changed |= ImGui::SliderFloat("Volume", &value.volume, 0, 4);
        changed |= ImGui::SliderFloat("Pitch", &value.pitch, 0.25f, 4);
        changed |= ImGui::Checkbox("Loop", &value.loop);
        changed |= ImGui::Checkbox("Play On Awake", &value.playOnAwake);
        changed |= ImGui::Checkbox("3D Spatial", &value.spatial);
        if (value.spatial)
        {
            changed |= ImGui::DragFloat("Min Distance", &value.minDistance, 0.1f, 0.01f, value.maxDistance - 0.01f);
            changed |= ImGui::DragFloat("Max Distance", &value.maxDistance, 0.1f, value.minDistance + 0.01f, 100000);
        }
        if (changed) setSettings(value);
    }
    bool AudioListenerComponent::serializePayload(std::vector<std::uint8_t>& payload) const
    {
        flatbuffers::FlatBufferBuilder builder;
        Serialization::AudioComponentPayloadBuilder root(builder);
        root.add_listener(true);
        Serialization::FinishAudioComponentPayloadBuffer(builder, root.Finish());
        payload.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
        return true;
    }
    bool AudioListenerComponent::deserializePayload(const std::uint32_t version, const std::span<const std::uint8_t> payload)
    {
        const auto* data = readAudio(version, payload);
        if (data && data->listener()) return true;
        if (data) LOG_ERROR("[Audio] Expected listener payload.");
        return false;
    }
}
