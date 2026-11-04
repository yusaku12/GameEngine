#include "Pch.h"
#include "Audio\AudioSystem.h"
#include "Components\Audio\AudioSourceComponent.h"
#include "Core\Scene\Scene.h"
#include "Core\System\GameSession.h"
#include <xaudio2.h>
#include <x3daudio.h>
#include <mmreg.h>
#include <cstring>
#include <atomic>
#include <bit>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\Transform.h"
#include "Core\GameObject\GameObject.h"
#pragma comment(lib, "xaudio2.lib")

namespace Engine
{
    namespace
    {
        template<typename T> struct VoiceDeleter
        {
            void operator()(T* voice) const noexcept { if (voice) voice->DestroyVoice(); }
        };
        struct Clip
        {
            WAVEFORMATEXTENSIBLE format{};
            std::vector<BYTE> bytes;
            std::filesystem::path path;
            std::uint32_t generation = 0;
        };
        bool audioResult(const HRESULT result, const char* operation)
        {
            if (SUCCEEDED(result))
                return true;
            LOG_ERROR("[Audio] {} failed: 0x{:08X}", operation, static_cast<std::uint32_t>(result));
            return false;
        }
        X3DAUDIO_VECTOR vector(const Vector3& value) { return { value.x, value.y, value.z }; }
    }

    struct AudioSystem::Impl : IXAudio2EngineCallback, IXAudio2VoiceCallback
    {
        std::atomic<HRESULT> playbackError{ S_OK };
        void STDMETHODCALLTYPE OnProcessingPassStart() override {}
        void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnCriticalError(const HRESULT error) override { playbackError.store(error); }
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, const HRESULT error) override { playbackError.store(error); }
        Microsoft::WRL::ComPtr<IXAudio2> engine;
        std::unique_ptr<IXAudio2MasteringVoice, VoiceDeleter<IXAudio2MasteringVoice>> master;
        X3DAUDIO_HANDLE spatial{};
        std::uint32_t channels = 0;
        std::vector<std::shared_ptr<const Clip>> clips;
        std::uint32_t nextGeneration = 1;
        struct Voice
        {
            std::shared_ptr<const Clip> clip;
            std::unique_ptr<IXAudio2SourceVoice, VoiceDeleter<IXAudio2SourceVoice>> native;
            Vector3 previousPosition = Vector3::Zero;
            std::vector<float> matrix;
            bool paused = false;
            bool missingListenerLogged = false;
        };
        std::unordered_map<const AudioSourceComponent*, Voice> voices;
        ObjectGUID listenerGuid;
        Vector3 previousListener = Vector3::Zero;
        bool listenerErrorLogged = false;
        bool paused = false;

        std::shared_ptr<const Clip> get(const AudioClipHandle handle) const noexcept
        {
            return handle.isValid() && handle.index < clips.size() && clips[handle.index]->generation == handle.generation
                ? clips[handle.index] : nullptr;
        }
    };

    AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}
    AudioSystem::~AudioSystem() { shutdown(); }
    AudioSystem& AudioSystem::instance() noexcept { static AudioSystem audio; return audio; }

    bool AudioSystem::initialize()
    {
        if (m_impl->engine)
            return true;
        if (!audioResult(XAudio2Create(m_impl->engine.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR), "XAudio2Create"))
            return false;
        if (!audioResult(m_impl->engine->RegisterForCallbacks(m_impl.get()), "Register audio callbacks"))
        {
            shutdown();
            return false;
        }
        IXAudio2MasteringVoice* voice = nullptr;
        if (!audioResult(m_impl->engine->CreateMasteringVoice(&voice), "CreateMasteringVoice"))
        {
            shutdown();
            return false;
        }
        m_impl->master.reset(voice);
        XAUDIO2_VOICE_DETAILS details{};
        voice->GetVoiceDetails(&details);
        m_impl->channels = details.InputChannels;
        DWORD mask = 0;
        if (!audioResult(voice->GetChannelMask(&mask), "GetChannelMask")
            || !audioResult(X3DAudioInitialize(mask, X3DAUDIO_SPEED_OF_SOUND, m_impl->spatial), "X3DAudioInitialize"))
        {
            shutdown();
            return false;
        }
        LOG_INFO("[Audio] XAudio2 output initialized ({} channels).", m_impl->channels);
        if (m_impl->paused)
            m_impl->engine->StopEngine();
        return true;
    }

    void AudioSystem::shutdown() noexcept
    {
        stopAll();
        m_impl->master.reset();
        if (m_impl->engine) m_impl->engine->UnregisterForCallbacks(m_impl.get());
        m_impl->engine.Reset();
        m_impl->playbackError.store(S_OK);
        m_impl->clips.clear();
        m_impl->paused = false;
    }

    AudioClipHandle AudioSystem::load(const std::filesystem::path& path)
    {
        if (path.empty())
        {
            LOG_ERROR("[Audio] WAV path is empty.");
            return {};
        }
        std::error_code error;
        const auto normalized = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            LOG_ERROR("[Audio] Cannot resolve WAV path: {}", error.message());
            return {};
        }
        for (std::size_t index = 0; index < m_impl->clips.size(); ++index)
            if (m_impl->clips[index]->path == normalized)
                return { static_cast<std::uint32_t>(index), m_impl->clips[index]->generation };
        std::ifstream file(normalized, std::ios::binary | std::ios::ate);
        const auto size = file ? file.tellg() : std::streampos(-1);
        if (size < 12 || size > 512 * 1024 * 1024)
        {
            LOG_ERROR("[Audio] WAV is unreadable or exceeds the 512 MiB limit.");
            return {};
        }
        std::vector<BYTE> data(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
        {
            LOG_ERROR("[Audio] Cannot read WAV data.");
            return {};
        }
        const auto u32 = [&data](const std::size_t offset)
        {
            std::uint32_t value;
            std::memcpy(&value, data.data() + offset, sizeof(value));
            return value;
        };
        if (std::memcmp(data.data(), "RIFF", 4) || std::memcmp(data.data() + 8, "WAVE", 4)
            || static_cast<std::uint64_t>(u32(4)) + 8 != data.size())
        {
            LOG_ERROR("[Audio] Invalid RIFF/WAVE header.");
            return {};
        }
        auto clip = std::make_shared<Clip>();
        bool formatFound = false;
        bool dataFound = false;
        std::size_t formatSize = 0;
        for (std::size_t cursor = 12; cursor < data.size();)
        {
            if (data.size() - cursor < 8)
            {
                LOG_ERROR("[Audio] Truncated WAV chunk header.");
                return {};
            }
            const std::size_t chunkSize = u32(cursor + 4);
            const std::size_t content = cursor + 8;
            if (chunkSize > data.size() - content || (chunkSize & 1u) > data.size() - content - chunkSize)
            {
                LOG_ERROR("[Audio] Truncated WAV chunk.");
                return {};
            }
            if (!std::memcmp(data.data() + cursor, "fmt ", 4))
            {
                if (formatFound || (chunkSize != 16 && chunkSize != 18 && chunkSize != sizeof(WAVEFORMATEXTENSIBLE)))
                {
                    LOG_ERROR("[Audio] Invalid or duplicate WAV format.");
                    return {};
                }
                std::memcpy(&clip->format, data.data() + content, chunkSize);
                formatFound = true;
                formatSize = chunkSize;
            }
            else if (!std::memcmp(data.data() + cursor, "data", 4))
            {
                if (dataFound || chunkSize == 0)
                {
                    LOG_ERROR("[Audio] Empty or duplicate WAV data.");
                    return {};
                }
                clip->bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(content),
                    data.begin() + static_cast<std::ptrdiff_t>(content + chunkSize));
                dataFound = true;
            }
            cursor = content + chunkSize + (chunkSize & 1u);
        }
        const auto& format = clip->format.Format;
        const bool pcm = format.wFormatTag == WAVE_FORMAT_PCM;
        const bool floating = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        constexpr GUID pcmSubtype{ WAVE_FORMAT_PCM, 0, 0x0010, { 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71 } };
        constexpr GUID floatSubtype{ WAVE_FORMAT_IEEE_FLOAT, 0, 0x0010, { 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71 } };
        const bool extended = format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.cbSize == 22
            && formatSize == sizeof(WAVEFORMATEXTENSIBLE)
            && (clip->format.SubFormat == pcmSubtype || clip->format.SubFormat == floatSubtype)
            && clip->format.Samples.wValidBitsPerSample > 0
            && clip->format.Samples.wValidBitsPerSample <= format.wBitsPerSample
            && (clip->format.dwChannelMask == 0
                || std::popcount(clip->format.dwChannelMask) == format.nChannels);
        if (!formatFound || !dataFound || (!pcm && !floating && !extended)
            || ((pcm || floating) && (format.cbSize != 0 || formatSize > 18))
            || (extended && clip->format.SubFormat == floatSubtype && clip->format.Samples.wValidBitsPerSample != 32)
            || format.nChannels < 1 || format.nChannels > 2 || format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 192000
            || (format.wBitsPerSample != 8 && format.wBitsPerSample != 16
                && format.wBitsPerSample != 24 && format.wBitsPerSample != 32)
            || ((floating || (extended && clip->format.SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT)) && format.wBitsPerSample != 32)
            || format.nBlockAlign == 0 || format.nBlockAlign != format.nChannels * format.wBitsPerSample / 8
            || format.nAvgBytesPerSec != format.nSamplesPerSec * format.nBlockAlign
            || clip->bytes.size() % format.nBlockAlign != 0)
        {
            LOG_ERROR("[Audio] Unsupported or inconsistent WAV format; use mono/stereo PCM or float.");
            return {};
        }
        if (m_impl->nextGeneration == 0 || m_impl->clips.size() >= UINT32_MAX)
        {
            LOG_ERROR("[Audio] Clip handle capacity exhausted.");
            return {};
        }
        clip->path = normalized;
        clip->generation = m_impl->nextGeneration++;
        const AudioClipHandle handle{ static_cast<std::uint32_t>(m_impl->clips.size()), clip->generation };
        m_impl->clips.push_back(std::move(clip));
        return handle;
    }

    std::filesystem::path AudioSystem::path(const AudioClipHandle handle) const
    {
        const auto clip = m_impl->get(handle);
        return clip ? clip->path : std::filesystem::path{};
    }
    bool AudioSystem::isClipValid(const AudioClipHandle handle) const noexcept { return m_impl->get(handle) != nullptr; }

    bool AudioSystem::play(AudioSourceComponent& source)
    {
        const auto clip = m_impl->get(source.clip());
        if (!GameSession::instance().isPlaying() || !source.isEnabled()
            || !source.getGameObject() || !source.getGameObject()->isActiveInHierarchy()
            || !clip || (source.settings().spatial && clip->format.Format.nChannels != 1))
        {
            LOG_ERROR("[Audio] Playback requires an active Play source and a valid clip; 3D WAV must be mono.");
            return false;
        }
        if (!initialize())
            return false;
        Impl::Voice voice;
        IXAudio2SourceVoice* native = nullptr;
        if (!audioResult(m_impl->engine->CreateSourceVoice(&native, &clip->format.Format,
            0, 4.0f, m_impl.get()), "CreateSourceVoice"))
            return false;
        voice.native.reset(native);
        voice.clip = clip;
        voice.previousPosition = source.getGameObject()->getWorldPosition();
        voice.matrix.resize(m_impl->channels);
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(clip->bytes.size());
        buffer.pAudioData = clip->bytes.data();
        buffer.LoopCount = source.settings().loop ? XAUDIO2_LOOP_INFINITE : 0;
        if (!audioResult(native->SetVolume(source.settings().spatial ? 0.0f : source.settings().volume), "SetVolume")
            || !audioResult(native->SetFrequencyRatio(source.settings().pitch), "SetFrequencyRatio")
            || !audioResult(native->SubmitSourceBuffer(&buffer), "SubmitSourceBuffer")
            || !audioResult(native->Start(), "Start"))
            return false;
        stop(source);
        m_impl->voices.emplace(&source, std::move(voice));
        return true;
    }

    void AudioSystem::stop(AudioSourceComponent& source) noexcept { m_impl->voices.erase(&source); }
    void AudioSystem::stopAll() noexcept
    {
        m_impl->voices.clear();
        m_impl->listenerGuid = {};
        m_impl->listenerErrorLogged = false;
    }
    bool AudioSystem::isPlaying(const AudioSourceComponent& source) const noexcept
    {
        const auto found = m_impl->voices.find(&source);
        if (found == m_impl->voices.end())
            return false;
        XAUDIO2_VOICE_STATE state{};
        found->second.native->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return state.BuffersQueued != 0;
    }
    double AudioSystem::playbackSeconds(const AudioSourceComponent& source) const noexcept
    {
        const auto found = m_impl->voices.find(&source);
        if (found == m_impl->voices.end())
            return 0;
        XAUDIO2_VOICE_STATE state{};
        found->second.native->GetState(&state);
        return static_cast<double>(state.SamplesPlayed) / found->second.clip->format.Format.nSamplesPerSec;
    }
    void AudioSystem::pause(AudioSourceComponent& source, const bool paused) noexcept
    {
        const auto found = m_impl->voices.find(&source);
        if (found == m_impl->voices.end())
        {
            LOG_ERROR("[Audio] Cannot pause a source without a playback voice.");
            return;
        }
        if (audioResult(paused ? found->second.native->Stop() : found->second.native->Start(), "Pause/Resume"))
            found->second.paused = paused;
    }
    bool AudioSystem::setPaused(const bool paused) noexcept
    {
        if (!m_impl->engine)
        {
            m_impl->paused = paused;
            return true;
        }
        if (paused)
            m_impl->engine->StopEngine();
        else if (!audioResult(m_impl->engine->StartEngine(), "Resume engine"))
            return false;
        m_impl->paused = paused;
        return true;
    }

    void AudioSystem::update(const Scene* active, const Scene& persistent, const float deltaTime) noexcept
    {
        const HRESULT playbackError = m_impl->playbackError.exchange(S_OK);
        if (!audioResult(playbackError, "Audio device/voice callback"))
        {
            stopAll();
            m_impl->master.reset();
            if (m_impl->engine) m_impl->engine->UnregisterForCallbacks(m_impl.get());
            m_impl->engine.Reset();
            m_impl->playbackError.store(S_OK);
            return;
        }
        const GameObject* listenerObject = nullptr;
        int listenerCount = 0;
        const auto collect = [&](const Scene& scene)
        {
            for (const auto& object : scene.getGameObjects())
                if (const auto* listener = object->getComponent<AudioListenerComponent>();
                    listener && listener->isEnabled() && object->isActiveInHierarchy())
                {
                    if (!listenerObject) listenerObject = object.get();
                    ++listenerCount;
                }
        };
        if (active) collect(*active);
        collect(persistent);
        if (listenerCount > 1 && !m_impl->listenerErrorLogged)
        {
            LOG_ERROR("[Audio] Multiple listeners are active; the first scene listener is used.");
            m_impl->listenerErrorLogged = true;
        }
        if (listenerCount <= 1) m_impl->listenerErrorLogged = false;
        X3DAUDIO_LISTENER listener{};
        if (listenerObject)
        {
            const Vector3 position = listenerObject->getWorldPosition();
            listener.Position = vector(position);
            listener.OrientFront = vector(Vector3::Transform(Vector3::UnitZ, listenerObject->getWorldRotation()));
            listener.OrientTop = vector(Vector3::Transform(Vector3::UnitY, listenerObject->getWorldRotation()));
            if (deltaTime > 0 && m_impl->listenerGuid == listenerObject->getGUID())
                listener.Velocity = vector((position - m_impl->previousListener) / deltaTime);
            m_impl->listenerGuid = listenerObject->getGUID();
            m_impl->previousListener = position;
        }
        for (auto iterator = m_impl->voices.begin(); iterator != m_impl->voices.end();)
        {
            const auto& source = *iterator->first;
            auto& voice = iterator->second;
            XAUDIO2_VOICE_STATE state{};
            voice.native->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued == 0)
            {
                iterator = m_impl->voices.erase(iterator);
                continue;
            }
            ++iterator;
            const auto& settings = source.settings();
            float volume = settings.volume;
            float ratio = settings.pitch;
            if (settings.spatial)
            {
                const Vector3 position = source.getGameObject()->getWorldPosition();
                if (!listenerObject)
                {
                    volume = 0;
                    if (!voice.missingListenerLogged)
                        LOG_ERROR("[Audio] Spatial playback is muted because no active Audio Listener exists.");
                    voice.missingListenerLogged = true;
                }
                else
                {
                    voice.missingListenerLogged = false;
                    X3DAUDIO_EMITTER emitter{};
                    emitter.Position = vector(position);
                    emitter.OrientFront = vector(Vector3::UnitZ);
                    emitter.OrientTop = vector(Vector3::UnitY);
                    emitter.ChannelCount = 1;
                    emitter.CurveDistanceScaler = settings.minDistance;
                    emitter.DopplerScaler = 1;
                    if (deltaTime > 0)
                        emitter.Velocity = vector((position - voice.previousPosition) / deltaTime);
                    X3DAUDIO_DSP_SETTINGS dsp{};
                    dsp.SrcChannelCount = 1;
                    dsp.DstChannelCount = m_impl->channels;
                    dsp.pMatrixCoefficients = voice.matrix.data();
                    X3DAudioCalculate(m_impl->spatial, &listener, &emitter,
                        X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER, &dsp);
                    audioResult(voice.native->SetOutputMatrix(m_impl->master.get(), 1, m_impl->channels,
                        voice.matrix.data()), "SetOutputMatrix");
                    ratio = std::clamp(ratio * dsp.DopplerFactor, 0.25f, 4.0f);
                    const float distance = Vector3::Distance(position, listenerObject->getWorldPosition());
                    volume *= std::clamp((settings.maxDistance - distance) /
                        (settings.maxDistance - settings.minDistance), 0.0f, 1.0f);
                }
                voice.previousPosition = position;
            }
            audioResult(voice.native->SetVolume(volume), "SetVolume");
            audioResult(voice.native->SetFrequencyRatio(ratio), "SetFrequencyRatio");
        }
    }
}
