#include "Pch.h"
#include "Assets\Effects\ParticleEffectAssetManager.h"
#include "Components\Effects\ParticleEffectComponent.h"
#include "Core\Containers\String.h"
#include "Core\Logging\Logger.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Generated\FlatBuffers\ParticleEffectAsset_generated.h"
#include "Generated\FlatBuffers\ParticleEffectComponent_generated.h"
#include <flatbuffers/flatbuffers.h>

namespace Engine
{
    namespace
    {
        std::filesystem::path normalizePath(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto result = path.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(path, error);
            if (result.empty() || error)
            {
                LOG_ERROR("[ParticleEffect] Invalid asset path: {}.", error.message());
                return {};
            }
            return result;
        }

        bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept
        {
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
        }

        bool validateSettings(const ParticleEffectSettings& settings)
        {
            ParticleEffectComponent component;
            return component.setSettings(settings);
        }

        bool saveAsset(const std::filesystem::path& path, const ParticleEffectAsset& asset)
        {
            ParticleEffectComponent component;
            std::vector<std::uint8_t> payload;
            if (!asset.guid.isValid() || !component.setSettings(asset.settings) || !component.serializePayload(payload))
            {
                LOG_ERROR("[ParticleEffect] Cannot serialize invalid asset settings.");
                return false;
            }
            flatbuffers::FlatBufferBuilder builder;
            const Serialization::AssetGuid guid(asset.guid.high, asset.guid.low);
            const auto settings = builder.CreateVector(payload);
            const auto root = Serialization::CreateParticleEffectAssetFile(builder, 15, &guid, settings);
            Serialization::FinishParticleEffectAssetFileBuffer(builder, root);
            return Serialization::FlatBufferWriter{}.saveAtomic(path,
                std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize()));
        }

        bool loadAsset(const std::filesystem::path& path, ParticleEffectAsset& destination)
        {
            Serialization::FlatBufferReader reader;
            if (!reader.open(path) || reader.size() < 8)
            {
                LOG_ERROR("[ParticleEffect] Cannot read asset file.");
                return false;
            }
            flatbuffers::Verifier verifier(reader.data(), reader.size());
            if (!Serialization::VerifyParticleEffectAssetFileBuffer(verifier))
            {
                LOG_ERROR("[ParticleEffect] Invalid asset file.");
                return false;
            }
            const auto* const file = Serialization::GetParticleEffectAssetFile(reader.data());
            if (file->version() < 1 || file->version() > 15
                || file->guid() == nullptr || file->settings() == nullptr)
            {
                LOG_ERROR("[ParticleEffect] Asset version or required data is invalid.");
                return false;
            }
            const ObjectGUID guid{ file->guid()->high(), file->guid()->low() };
            const auto* const bytes = file->settings();
            flatbuffers::Verifier settingsVerifier(bytes->data(), bytes->size());
            if (!guid.isValid() || bytes->size() < 8
                || !Serialization::VerifyParticleEffectComponentPayloadBuffer(settingsVerifier))
            {
                LOG_ERROR("[ParticleEffect] Invalid asset identity or settings payload.");
                return false;
            }
            const auto* const settings = Serialization::GetParticleEffectComponentPayload(bytes->data());
            if (settings->asset_guid() != nullptr || settings->asset_path() != nullptr)
            {
                LOG_ERROR("[ParticleEffect] Asset settings cannot reference another asset.");
                return false;
            }
            ParticleEffectComponent component;
            const std::uint32_t payloadVersion = file->version() == 1 ? 1 : file->version() + 1;
            if (!component.deserializePayload(payloadVersion, std::span<const std::uint8_t>(bytes->data(), bytes->size())))
                return false;
            destination = { guid, component.settings() };
            return true;
        }
    }

    ParticleEffectAssetManager& ParticleEffectAssetManager::instance() noexcept
    {
        static ParticleEffectAssetManager manager;
        return manager;
    }

    ParticleEffectAssetManager::Entry* ParticleEffectAssetManager::findEntry(const ParticleEffectAssetHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        auto& entry = m_entries[handle.index];
        return entry.asset && entry.generation == handle.generation ? &entry : nullptr;
    }

    const ParticleEffectAssetManager::Entry* ParticleEffectAssetManager::findEntry(const ParticleEffectAssetHandle handle) const noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        const auto& entry = m_entries[handle.index];
        return entry.asset && entry.generation == handle.generation ? &entry : nullptr;
    }

    ParticleEffectAssetHandle ParticleEffectAssetManager::add(ParticleEffectAsset asset, std::filesystem::path path)
    {
        if (m_nextGeneration == 0 || m_entries.size() >= UINT32_MAX)
        {
            LOG_ERROR("[ParticleEffect] Asset registry capacity exhausted.");
            return {};
        }
        const auto index = static_cast<std::uint32_t>(m_entries.size());
        const bool dirty = path.empty();
        m_entries.push_back({ std::make_shared<const ParticleEffectAsset>(std::move(asset)),
            std::move(path), m_nextGeneration++, dirty });
        return { index, m_entries.back().generation };
    }

    ParticleEffectAssetHandle ParticleEffectAssetManager::create(const ParticleEffectSettings& settings)
    {
        if (!validateSettings(settings))
            return {};
        ParticleEffectAsset asset{ ObjectGUID::generate(), settings };
        const std::scoped_lock lock(m_mutex);
        for (const auto& entry : m_entries)
            if (entry.asset && entry.asset->guid == asset.guid)
            {
                LOG_ERROR("[ParticleEffect] Generated a duplicate asset GUID.");
                return {};
            }
        return add(std::move(asset), {});
    }

    ParticleEffectAssetHandle ParticleEffectAssetManager::load(const std::filesystem::path& path, const ObjectGUID expectedGuid)
    {
        const auto normalized = normalizePath(path);
        if (normalized.empty())
            return {};
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            const auto& entry = m_entries[index];
            if (entry.asset && !entry.path.empty() && samePath(entry.path, normalized))
            {
                if (expectedGuid.isValid() && entry.asset->guid != expectedGuid)
                {
                    LOG_ERROR("[ParticleEffect] Asset GUID does not match the saved reference.");
                    return {};
                }
                return { static_cast<std::uint32_t>(index), entry.generation };
            }
        }
        ParticleEffectAsset asset;
        if (!loadAsset(normalized, asset))
            return {};
        if (expectedGuid.isValid() && expectedGuid != asset.guid)
        {
            LOG_ERROR("[ParticleEffect] Asset GUID does not match the saved reference.");
            return {};
        }
        for (const auto& entry : m_entries)
            if (entry.asset && entry.asset->guid == asset.guid)
            {
                LOG_ERROR("[ParticleEffect] Asset GUID is already registered from another path.");
                return {};
            }
        return add(std::move(asset), normalized);
    }

    bool ParticleEffectAssetManager::update(const ParticleEffectAssetHandle handle, const ParticleEffectSettings& settings)
    {
        if (!validateSettings(settings))
            return false;
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Cannot update a stale asset handle.");
            return false;
        }
        entry->asset = std::make_shared<const ParticleEffectAsset>(ParticleEffectAsset{ entry->asset->guid, settings });
        entry->dirty = true;
        return true;
    }

    bool ParticleEffectAssetManager::save(const ParticleEffectAssetHandle handle, const std::filesystem::path& path)
    {
        auto target = path.empty() ? std::filesystem::path{} : normalizePath(path);
        if (!path.empty() && target.empty())
            return false;
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[ParticleEffect] Cannot save a stale asset handle.");
            return false;
        }
        if (path.empty())
            target = entry->path;
        if (target.empty())
        {
            LOG_ERROR("[ParticleEffect] Save As is required for an unsaved asset.");
            return false;
        }
        if (!entry->path.empty() && !samePath(entry->path, target))
        {
            LOG_ERROR("[ParticleEffect] Create a unique asset before saving to a different path.");
            return false;
        }
        for (const auto& other : m_entries)
            if (&other != entry && other.asset && !other.path.empty() && samePath(other.path, target))
            {
                LOG_ERROR("[ParticleEffect] Asset save path is already registered.");
                return false;
            }
        if (!saveAsset(target, *entry->asset))
            return false;
        entry->path = std::move(target);
        entry->dirty = false;
        return true;
    }

    bool ParticleEffectAssetManager::reload(const ParticleEffectAssetHandle handle)
    {
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr || entry->path.empty())
        {
            LOG_ERROR("[ParticleEffect] Cannot reload a stale or unsaved asset.");
            return false;
        }
        ParticleEffectAsset loaded;
        if (!loadAsset(entry->path, loaded))
            return false;
        if (loaded.guid != entry->asset->guid)
        {
            LOG_ERROR("[ParticleEffect] Reload would replace the asset identity.");
            return false;
        }
        entry->asset = std::make_shared<const ParticleEffectAsset>(std::move(loaded));
        entry->dirty = false;
        return true;
    }

    std::shared_ptr<const ParticleEffectAsset> ParticleEffectAssetManager::get(const ParticleEffectAssetHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry ? entry->asset : nullptr;
    }

    std::filesystem::path ParticleEffectAssetManager::getPath(const ParticleEffectAssetHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry ? entry->path : std::filesystem::path{};
    }

    bool ParticleEffectAssetManager::isDirty(const ParticleEffectAssetHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry && entry->dirty;
    }

    std::vector<ParticleEffectAssetHandle> ParticleEffectAssetManager::getAllHandles() const
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<ParticleEffectAssetHandle> handles;
        for (std::size_t index = 0; index < m_entries.size(); ++index)
            if (m_entries[index].asset)
                handles.push_back({ static_cast<std::uint32_t>(index), m_entries[index].generation });
        return handles;
    }

    void ParticleEffectAssetManager::clear()
    {
        const std::scoped_lock lock(m_mutex);
        m_entries.clear();
    }
}
