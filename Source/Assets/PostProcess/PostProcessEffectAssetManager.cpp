#include "Pch.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Assets\PostProcess\Serialization\PostProcessEffectSerializer.h"
#include "Core\Containers\String.h"
#include "Graphics\Shader\Shader.h"
#include "Graphics\Shader\ShaderCompiler.h"
#include "Core\Serialization\FlatBufferReader.h"
#include <atomic>
#include <cmath>
#include <iomanip>
#include <sstream>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathFunction.h"

namespace Engine
{
    namespace
    {
        std::atomic_uint64_t stagingSequence = 1;

        bool samePath(const std::filesystem::path& left, const std::filesystem::path& right) noexcept
        {
            return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
        }

        std::string pathText(const std::filesystem::path& path)
        {
            const auto text = String::fromWide(path.c_str());
            return text.size() == 0 ? std::string{} : std::string(text.c_str(), text.size());
        }

        bool validIdentifier(const std::string& value)
        {
            if (value.empty() || value.find('\0') != std::string::npos
                || !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::all_of(value.begin() + 1, value.end(), [](const char character)
            {
                return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
            });
        }

        bool validateDefinition(const PostProcessEffectAsset& asset)
        {
            if (!asset.guid.isValid() || asset.name.empty() || asset.name.find('\0') != std::string::npos
                || asset.sourcePath.empty() || !validIdentifier(asset.entryPoint)
                || (asset.stage != PostProcessStage::BeforeToneMapping && asset.stage != PostProcessStage::AfterToneMapping)
                || asset.resolutionDivisor == 0 || asset.resolutionDivisor > 16 || asset.parameters.size() > 16)
            {
                LOG_ERROR("[PostProcessEffect] Invalid effect identity, entry point, stage, resolution, or parameter count.");
                return false;
            }
            for (std::size_t index = 0; index < asset.parameters.size(); ++index)
            {
                const auto& parameter = asset.parameters[index];
                if (parameter.name.empty() || parameter.name.find('\0') != std::string::npos
                    || !std::isfinite(parameter.defaultValue) || !std::isfinite(parameter.minimum)
                    || !std::isfinite(parameter.maximum) || parameter.minimum > parameter.maximum
                    || parameter.defaultValue < parameter.minimum || parameter.defaultValue > parameter.maximum)
                {
                    LOG_ERROR("[PostProcessEffect] Invalid parameter metadata at index {}.", index);
                    return false;
                }
                for (std::size_t previous = 0; previous < index; ++previous)
                    if (asset.parameters[previous].name == parameter.name)
                    {
                        LOG_ERROR("[PostProcessEffect] Duplicate parameter name '{}'.", parameter.name);
                        return false;
                    }
            }
            return true;
        }

        std::string guidText(const ObjectGUID guid)
        {
            std::ostringstream name;
            name << std::hex << std::setfill('0') << std::setw(16) << guid.high << std::setw(16) << guid.low << ".cso";
            return name.str();
        }

        std::filesystem::path normalizedSource(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto normalized = std::filesystem::weakly_canonical(path, error);
            if (error || normalized.empty() || !std::filesystem::is_regular_file(normalized, error) || error)
            {
                LOG_ERROR("[PostProcessEffect] HLSL source is not a readable file: '{}'.", pathText(path));
                return {};
            }
            return normalized;
        }
    }

    PostProcessEffectAssetManager& PostProcessEffectAssetManager::instance() noexcept
    {
        static PostProcessEffectAssetManager manager;
        return manager;
    }

    std::filesystem::path PostProcessEffectAssetManager::normalizePath(const std::filesystem::path& path)
    {
        std::error_code error;
        const auto result = path.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(path, error);
        if (result.empty() || error)
        {
            LOG_ERROR("[PostProcessEffect] Invalid asset path '{}': {}.", pathText(path), error.message());
            return {};
        }
        return result;
    }

    PostProcessEffectAssetManager::Entry* PostProcessEffectAssetManager::findEntry(const PostProcessEffectHandle handle) noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        auto& entry = m_entries[handle.index];
        return entry.asset && entry.generation == handle.generation ? &entry : nullptr;
    }

    const PostProcessEffectAssetManager::Entry* PostProcessEffectAssetManager::findEntry(
        const PostProcessEffectHandle handle) const noexcept
    {
        if (!handle.isValid() || handle.index >= m_entries.size())
            return nullptr;
        const auto& entry = m_entries[handle.index];
        return entry.asset && entry.generation == handle.generation ? &entry : nullptr;
    }

    PostProcessEffectHandle PostProcessEffectAssetManager::add(
        PostProcessEffectAsset asset, std::filesystem::path path)
    {
        if (m_nextGeneration == 0 || m_entries.size() >= UINT32_MAX)
        {
            LOG_ERROR("[PostProcessEffect] Handle capacity exhausted.");
            return {};
        }
        const auto snapshot = std::make_shared<const PostProcessEffectAsset>(std::move(asset));
        for (std::size_t index = 0; index < m_entries.size(); ++index)
            if (!m_entries[index].asset)
            {
                const bool dirty = path.empty();
                m_entries[index] = { snapshot, std::move(path), m_nextGeneration++, dirty };
                return { static_cast<std::uint32_t>(index), m_entries[index].generation };
            }
        const auto index = static_cast<std::uint32_t>(m_entries.size());
        const bool dirty = path.empty();
        m_entries.push_back({ snapshot, std::move(path), m_nextGeneration++, dirty });
        return { index, m_entries.back().generation };
    }

    bool PostProcessEffectAssetManager::loadCompiled(PostProcessEffectAsset& asset) const
    {
        if (!validateDefinition(asset))
            return false;
        const std::filesystem::path bytecodeFile(asset.compiledBytecodeFile);
        if (asset.compiledBytecodeFile.empty() || bytecodeFile.filename() != bytecodeFile
            || bytecodeFile.extension() != L".cso")
        {
            LOG_ERROR("[PostProcessEffect] Invalid compiled bytecode cache name for '{}'.", asset.name);
            return false;
        }
        const auto bytecode = std::filesystem::current_path()
            / "Assets\\Shaders\\Compiled\\PostProcessCustom" / bytecodeFile;
        auto shader = std::make_shared<DX12Shader>();
        if (!shader->load({ .bytecodePath = bytecode }))
        {
            LOG_ERROR("[PostProcessEffect] Compiled shader is missing or invalid for '{}'. Recompile the asset in the Editor.",
                asset.name);
            return false;
        }
        asset.pixelShader = std::move(shader);
        return true;
    }

    bool PostProcessEffectAssetManager::compile(PostProcessEffectAsset& asset) const
    {
        if (!validateDefinition(asset))
            return false;
        asset.sourcePath = normalizedSource(asset.sourcePath);
        if (asset.sourcePath.empty())
            return false;
        const ObjectGUID revision = ObjectGUID::generate();
        const std::string finalName = guidText(asset.guid).substr(0, 32) + "_"
            + guidText(revision).substr(0, 32) + ".cso";
        const auto output = std::filesystem::current_path()
            / "Assets\\Shaders\\Compiled\\PostProcessCustom" / finalName;
        std::error_code error;
        std::filesystem::create_directories(output.parent_path(), error);
        if (error)
        {
            LOG_ERROR("[PostProcessEffect] Cannot create shader cache directory '{}': {}.",
                pathText(output.parent_path()), error.message());
            return false;
        }
        const auto sequence = stagingSequence.fetch_add(1, std::memory_order_relaxed);
        const auto staging = std::filesystem::path(output.wstring() + L".pending." + std::to_wstring(sequence));
        const auto shaderRoot = std::filesystem::current_path() / "Assets\\Shaders";
        const ShaderCompileDesc description{
            .sourcePath = asset.sourcePath,
            .outputPath = staging,
            .entryPoint = asset.entryPoint,
            .stage = ShaderStage::Pixel,
            .shaderModel = ShaderModel::SM_6_0,
            .languageVersion = HlslLanguageVersion::Hlsl2021,
            .includeDirectories = { asset.sourcePath.parent_path(), shaderRoot },
            .debug = false,
            .optimize = true,
        };
        const auto result = ShaderCompiler{}.compile(description);
        if (!result.success)
        {
            LOG_ERROR("[PostProcessEffect] DXC compilation failed for '{}':\n{}",
                pathText(asset.sourcePath), result.diagnostics);
            std::filesystem::remove(staging, error);
            return false;
        }
        auto shader = std::make_shared<DX12Shader>();
        if (!shader->load({ .bytecodePath = staging }) || !shader->isCompiled())
        {
            LOG_ERROR("[PostProcessEffect] Could not load the staged shader bytecode for '{}'.", asset.name);
            std::filesystem::remove(staging, error);
            return false;
        }
        if (!MoveFileExW(staging.c_str(), output.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            LOG_ERROR("[PostProcessEffect] Could not publish compiled shader cache '{}': Windows error {}.",
                pathText(output), GetLastError());
            std::filesystem::remove(staging, error);
            return false;
        }
        asset.compiledBytecodeFile = finalName;
        asset.pixelShader = std::move(shader);
        return true;
    }

    PostProcessEffectHandle PostProcessEffectAssetManager::load(
        const std::filesystem::path& path, const ObjectGUID expectedGuid)
    {
        const auto normalized = normalizePath(path);
        if (normalized.empty())
            return {};
        {
            const std::scoped_lock lock(m_mutex);
            for (std::size_t index = 0; index < m_entries.size(); ++index)
            {
                const auto& entry = m_entries[index];
                if (entry.asset && !entry.path.empty() && samePath(entry.path, normalized))
                {
                    if (expectedGuid.isValid() && entry.asset->guid != expectedGuid)
                    {
                        LOG_ERROR("[PostProcessEffect] GUID mismatch for asset '{}'.", pathText(normalized));
                        return {};
                    }
                    return { static_cast<std::uint32_t>(index), entry.generation };
                }
            }
        }
        PostProcessEffectAsset asset;
        if (!Serialization::PostProcessEffectSerializer{}.load(normalized, asset)
            || (expectedGuid.isValid() && asset.guid != expectedGuid)
            || !loadCompiled(asset))
        {
            if (expectedGuid.isValid() && asset.guid.isValid() && asset.guid != expectedGuid)
                LOG_ERROR("[PostProcessEffect] GUID does not match the asset reference: '{}'.", pathText(normalized));
            return {};
        }
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < m_entries.size(); ++index)
        {
            const auto& entry = m_entries[index];
            if (entry.asset && (entry.asset->guid == asset.guid || (!entry.path.empty() && samePath(entry.path, normalized))))
            {
                LOG_ERROR("[PostProcessEffect] Asset GUID or path is already registered: '{}'.", pathText(normalized));
                return {};
            }
        }
        return add(std::move(asset), normalized);
    }

    PostProcessEffectHandle PostProcessEffectAssetManager::create(PostProcessEffectAsset asset)
    {
        if (!asset.guid.isValid())
            asset.guid = ObjectGUID::generate();
        {
            const std::scoped_lock lock(m_mutex);
            for (const auto& entry : m_entries)
                if (entry.asset && entry.asset->guid == asset.guid)
                {
                    LOG_ERROR("[PostProcessEffect] Cannot create a duplicate effect GUID.");
                    return {};
                }
        }
        if (!compile(asset))
            return {};
        const std::scoped_lock lock(m_mutex);
        for (const auto& entry : m_entries)
            if (entry.asset && entry.asset->guid == asset.guid)
            {
                LOG_ERROR("[PostProcessEffect] Cannot create a duplicate effect GUID.");
                return {};
            }
        return add(std::move(asset), {});
    }

    bool PostProcessEffectAssetManager::update(const PostProcessEffectHandle handle, PostProcessEffectAsset asset)
    {
        {
            const std::scoped_lock lock(m_mutex);
            const auto* const entry = findEntry(handle);
            if (entry == nullptr)
            {
                LOG_ERROR("[PostProcessEffect] Cannot update a stale asset handle.");
                return false;
            }
            asset.guid = entry->asset->guid;
        }
        if (!compile(asset))
            return false;
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[PostProcessEffect] Asset was unloaded while compiling; update discarded.");
            return false;
        }
        entry->asset = std::make_shared<const PostProcessEffectAsset>(std::move(asset));
        entry->dirty = true;
        return true;
    }

    bool PostProcessEffectAssetManager::save(const PostProcessEffectHandle handle, const std::filesystem::path& path)
    {
        auto target = path.empty() ? std::filesystem::path{} : normalizePath(path);
        if (!path.empty() && target.empty())
            return false;
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[PostProcessEffect] Cannot save a stale asset handle.");
            return false;
        }
        if (path.empty())
            target = entry->path;
        if (target.empty())
        {
            LOG_ERROR("[PostProcessEffect] Unsaved effect asset requires Save As.");
            return false;
        }
        for (const auto& other : m_entries)
            if (&other != entry && other.asset && !other.path.empty() && samePath(other.path, target))
            {
                LOG_ERROR("[PostProcessEffect] Save path belongs to another effect asset: '{}'.", pathText(target));
                return false;
            }
        if (!validateDefinition(*entry->asset)
            || !Serialization::PostProcessEffectSerializer{}.save(target, *entry->asset))
            return false;
        entry->path = std::move(target);
        entry->dirty = false;
        return true;
    }

    bool PostProcessEffectAssetManager::reload(const PostProcessEffectHandle handle)
    {
        std::filesystem::path path;
        {
            const std::scoped_lock lock(m_mutex);
            const auto* const entry = findEntry(handle);
            if (entry == nullptr || entry->path.empty())
            {
                LOG_ERROR("[PostProcessEffect] Cannot reload a stale or unsaved effect asset.");
                return false;
            }
            path = entry->path;
        }
        PostProcessEffectAsset loaded;
        if (!Serialization::PostProcessEffectSerializer{}.load(path, loaded) || !loadCompiled(loaded))
            return false;
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr || entry->asset->guid != loaded.guid)
        {
            LOG_ERROR("[PostProcessEffect] Asset changed while reloading; previous snapshot retained.");
            return false;
        }
        entry->asset = std::make_shared<const PostProcessEffectAsset>(std::move(loaded));
        entry->dirty = false;
        return true;
    }

    std::shared_ptr<const PostProcessEffectAsset> PostProcessEffectAssetManager::get(
        const PostProcessEffectHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry ? entry->asset : nullptr;
    }

    std::filesystem::path PostProcessEffectAssetManager::getPath(const PostProcessEffectHandle handle) const
    {
        const std::scoped_lock lock(m_mutex);
        const auto* const entry = findEntry(handle);
        return entry ? entry->path : std::filesystem::path{};
    }

    PostProcessEffectHandle PostProcessEffectAssetManager::findByGuid(const ObjectGUID guid) const
    {
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < m_entries.size(); ++index)
            if (m_entries[index].asset && m_entries[index].asset->guid == guid)
                return { static_cast<std::uint32_t>(index), m_entries[index].generation };
        return {};
    }

    std::vector<PostProcessEffectHandle> PostProcessEffectAssetManager::getAllHandles() const
    {
        const std::scoped_lock lock(m_mutex);
        std::vector<PostProcessEffectHandle> handles;
        handles.reserve(m_entries.size());
        for (std::size_t index = 0; index < m_entries.size(); ++index)
            if (m_entries[index].asset)
                handles.push_back({ static_cast<std::uint32_t>(index), m_entries[index].generation });
        return handles;
    }

    bool PostProcessEffectAssetManager::unload(const PostProcessEffectHandle handle)
    {
        const std::scoped_lock lock(m_mutex);
        auto* const entry = findEntry(handle);
        if (entry == nullptr)
        {
            LOG_ERROR("[PostProcessEffect] Cannot unload a stale asset handle.");
            return false;
        }
        entry->asset.reset();
        entry->path.clear();
        entry->dirty = false;
        return true;
    }

    void PostProcessEffectAssetManager::clear()
    {
        const std::scoped_lock lock(m_mutex);
        m_entries.clear();
        m_reportedStaleHandles.clear();
    }

    void PostProcessEffectAssetManager::reportStaleHandleOnce(const PostProcessEffectHandle handle) const
    {
        if (!handle.isValid())
            return;
        const auto key = (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
        try
        {
            const std::scoped_lock lock(m_mutex);
            if (m_reportedStaleHandles.insert(key).second)
                LOG_ERROR("[PostProcessEffect] Stale custom Effect handle (index {}, generation {}).",
                    handle.index, handle.generation);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PostProcessEffect] Could not record stale-handle diagnostic.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[PostProcessEffect] Stale-handle diagnostic capacity exceeded.");
        }
    }

    bool appendPostProcessEffectSnapshot(PostProcessCameraSnapshot& destination,
        const PostProcessEffectHandle handle, const std::array<float, 16>& parameters,
        const float contribution) noexcept
    {
        if (!std::isfinite(contribution) || contribution < 0 || contribution > 1)
        {
            LOG_ERROR("[PostProcessEffect] Snapshot contribution must be finite and in [0,1].");
            return false;
        }
        if (contribution == 0)
            return true;
        const auto asset = PostProcessEffectAssetManager::instance().get(handle);
        if (!asset)
        {
            PostProcessEffectAssetManager::instance().reportStaleHandleOnce(handle);
            return true;
        }
        if (!asset->pixelShader || !asset->pixelShader->isCompiled())
        {
            LOG_ERROR("[PostProcessEffect] '{}' has no compiled shader snapshot.", asset->name);
            return true;
        }
        PostProcessEffectPassSnapshot snapshot{
            .pixelShader = asset->pixelShader,
            .stage = asset->stage,
            .parameters = parameters,
            .resolutionDivisor = asset->resolutionDivisor,
            .contribution = contribution,
        };
        for (std::size_t index = 0; index < asset->parameters.size(); ++index)
        {
            const float value = parameters[index];
            const auto& definition = asset->parameters[index];
            if (!std::isfinite(value) || value < definition.minimum || value > definition.maximum)
            {
                LOG_ERROR("[PostProcessEffect] '{}' parameter '{}' is invalid in the render snapshot.",
                    asset->name, definition.name);
                return false;
            }
        }
        try
        {
            destination.customEffects.push_back(std::move(snapshot));
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PostProcessEffect] Cannot allocate the Camera effect snapshot.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[PostProcessEffect] Camera effect snapshot capacity exceeded.");
            return false;
        }
        return true;
    }
}
