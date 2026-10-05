#include "Pch.h"
#include "ShaderManager.h"

namespace Engine
{
    namespace
    {
        void collectIncludes(const std::filesystem::path& sourcePath,
            const std::vector<std::filesystem::path>& includeDirectories,
            std::vector<std::filesystem::path>& dependencies)
        {
            std::ifstream file(sourcePath);
            std::string line;
            while (std::getline(file, line))
            {
                const std::size_t includeBegin = line.find("#include");
                const std::size_t quoteBegin = includeBegin == std::string::npos ? std::string::npos : line.find_first_of("\"<", includeBegin);
                const std::size_t quoteEnd = quoteBegin == std::string::npos ? std::string::npos : line.find_first_of("\">", quoteBegin + 1);
                if (quoteEnd == std::string::npos)
                    continue;

                const std::filesystem::path includeName = line.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1);
                std::vector<std::filesystem::path> candidates;
                if (includeName.is_absolute())
                    candidates.push_back(includeName);
                else
                {
                    candidates.push_back(sourcePath.parent_path() / includeName);
                    for (const std::filesystem::path& directory : includeDirectories)
                        candidates.push_back(directory / includeName);
                }

                for (const std::filesystem::path& candidate : candidates)
                {
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(candidate, error))
                        continue;
                    std::filesystem::path includePath = std::filesystem::weakly_canonical(candidate, error);
                    if (error)
                    {
                        error.clear();
                        includePath = std::filesystem::absolute(candidate, error);
                    }
                    if (error)
                        continue;
                    includePath = includePath.lexically_normal();
                    if (std::find(dependencies.begin(), dependencies.end(), includePath) != dependencies.end())
                        break;
                    dependencies.push_back(includePath);
                    collectIncludes(includePath, includeDirectories, dependencies);
                    break;
                }
            }
        }
    }

    ShaderManager::~ShaderManager() { shutdown(); }

    bool ShaderManager::initialize(const ShaderMode mode, const std::filesystem::path& shaderRoot, ShaderChangedCallback callback)
    {
        if (m_running || shaderRoot.empty())
            return false;
        m_mode = mode;
        m_shaderRoot = shaderRoot;
        m_callback = std::move(callback);
        m_running = true;
        try
        {
            m_worker = std::thread(&ShaderManager::compileWorker, this);
        }
        catch (const std::system_error& exception)
        {
            m_running = false;
            LOG_ERROR("[ShaderManager] Could not start shader compiler worker: {}", exception.what());
            return false;
        }

        if (m_mode != ShaderMode::Runtime)
        {
            try
            {
                if (!m_watcher.start(m_shaderRoot))
                    LOG_WARNING("[ShaderHotReload] File watcher could not start: {}", m_shaderRoot.string());
            }
            catch (const std::exception& exception)
            {
                LOG_ERROR("[ShaderHotReload] File watcher initialization failed: {}", exception.what());
            }
        }
        return true;
    }

    void ShaderManager::shutdown()
    {
        if (!m_running.exchange(false))
            return;
        m_watcher.stop();
        m_condition.notify_all();
        if (m_worker.joinable())
            m_worker.join();
        std::scoped_lock lock(m_mutex);
        m_entries.clear();
        while (!m_requests.empty()) m_requests.pop();
        while (!m_results.empty()) m_results.pop();
        m_recompilePending.clear();
    }

    ShaderID ShaderManager::registerShader(const ShaderCompileDesc& compileDesc)
    {
        ShaderCompileDesc normalized = compileDesc;
        normalized.sourcePath = std::filesystem::absolute(normalized.sourcePath);
        normalized.outputPath = std::filesystem::absolute(normalized.outputPath);
        const std::vector<std::filesystem::path> dependencies =
            collectDependencies(normalized.sourcePath, normalized.includeDirectories);
        std::scoped_lock lock(m_mutex);
        if (m_nextId == 0)
        {
            LOG_ERROR("[ShaderManager] Shader ID space exhausted");
            return 0;
        }

        const ShaderID id = m_nextId++;
        const bool inserted = m_entries.emplace(id, Entry{
            .compileDesc = std::move(normalized),
            .dependencies = dependencies,
            .shader = std::make_shared<DX12Shader>()
            }).second;
        if (!inserted)
        {
            LOG_ERROR("[ShaderManager] Shader ID collision detected: {}", id);
            return 0;
        }

        return id;
    }

    bool ShaderManager::loadAll()
    {
        std::scoped_lock lock(m_mutex);
        bool loaded = true;
        for (auto& [id, entry] : m_entries)
        {
            entry.status = ShaderStatus::Unloaded;
            if (!entry.shader->load(DX12ShaderConfig{ .bytecodePath = entry.compileDesc.outputPath }))
            {
                loaded = false;
                continue;
            }
            entry.status = ShaderStatus::Loaded;
        }
        return loaded;
    }

    void ShaderManager::processHotReload()
    {
        if (m_watcher.consumeOverflow())
            recompileAll();

        for (const auto& changedPath : m_watcher.consumeChanges())
        {
            std::scoped_lock lock(m_mutex);
            for (const auto& [id, entry] : m_entries)
            {
                const auto absoluteChangedPath = std::filesystem::absolute(changedPath);
                if (entry.compileDesc.sourcePath == absoluteChangedPath
                    || std::find(entry.dependencies.begin(), entry.dependencies.end(), absoluteChangedPath) != entry.dependencies.end())
                    enqueueCompile(id);
            }
        }
        std::queue<CompileResult> results;
        {
            std::scoped_lock lock(m_mutex);
            results.swap(m_results);
        }
        while (!results.empty())
        {
            const CompileResult compileResult = std::move(results.front());
            results.pop();
            {
                std::scoped_lock lock(m_mutex);
                auto entry = m_entries.find(compileResult.id);
                if (entry == m_entries.end())
                    continue;
                if (m_recompilePending.erase(compileResult.id) != 0)
                {
                    entry->second.status = entry->second.shader != nullptr && entry->second.shader->isCompiled()
                        ? ShaderStatus::Loaded : ShaderStatus::Unloaded;
                    enqueueCompile(compileResult.id);
                    continue;
                }
                if (!compileResult.result.success)
                {
                    entry->second.status = ShaderStatus::ReloadFailed;
                    LOG_ERROR("[ShaderHotReload] {}", compileResult.result.diagnostics);
                    continue;
                }
                auto replacement = std::make_shared<DX12Shader>();
                if (!replacement->load(DX12ShaderConfig{ .bytecodePath = compileResult.result.outputPath }))
                {
                    entry->second.status = ShaderStatus::ReloadFailed;
                    continue;
                }
                entry->second.shader = std::move(replacement);
                entry->second.dependencies = compileResult.dependencies;
                entry->second.status = ShaderStatus::Loaded;
            }
            if (m_callback)
                m_callback(compileResult.id);
        }
    }

    void ShaderManager::recompileShader(const ShaderID id)
    {
        std::scoped_lock lock(m_mutex);
        enqueueCompile(id);
    }

    void ShaderManager::recompileAll()
    {
        std::scoped_lock lock(m_mutex);
        for (const auto& [id, entry] : m_entries)
            enqueueCompile(id);
    }

    bool ShaderManager::reloadAll()
    {
        std::vector<ShaderID> reloadedIds;
        bool allSucceeded = true;
        {
            std::scoped_lock lock(m_mutex);
            reloadedIds.reserve(m_entries.size());
            for (auto& [id, entry] : m_entries)
            {
                auto reloaded = std::make_shared<DX12Shader>();
                if (reloaded->load(DX12ShaderConfig{ .bytecodePath = entry.compileDesc.outputPath }))
                {
                    entry.shader = std::move(reloaded);
                    entry.status = ShaderStatus::Loaded;
                    reloadedIds.push_back(id);
                }
                else
                {
                    entry.status = ShaderStatus::ReloadFailed;
                    allSucceeded = false;
                }
            }
        }
        for (const ShaderID id : reloadedIds)
        {
            if (m_callback)
                m_callback(id);
        }
        return allSucceeded;
    }

    std::shared_ptr<const DX12Shader> ShaderManager::get(const ShaderID id) const
    {
        std::scoped_lock lock(m_mutex);
        const auto entry = m_entries.find(id);
        return entry == m_entries.end() ? nullptr : entry->second.shader;
    }

    std::vector<std::filesystem::path> ShaderManager::collectDependencies(
        const std::filesystem::path& sourcePath,
        const std::vector<std::filesystem::path>& includeDirectories)
    {
        std::vector<std::filesystem::path> dependencies;
        collectIncludes(sourcePath, includeDirectories, dependencies);
        return dependencies;
    }

    ShaderStatus ShaderManager::getStatus(const ShaderID id) const
    {
        std::scoped_lock lock(m_mutex);
        const auto entry = m_entries.find(id);
        return entry == m_entries.end() ? ShaderStatus::Unloaded : entry->second.status;
    }

    ShaderDetails ShaderManager::getShaderDetails(const ShaderID id) const
    {
        std::scoped_lock lock(m_mutex);
        const auto entry = m_entries.find(id);
        if (entry == m_entries.end())
            return {};

        return ShaderDetails{
            .id = id,
            .compileDesc = entry->second.compileDesc,
            .dependencies = entry->second.dependencies,
            .status = entry->second.status,
            .hasValidBytecode = entry->second.shader != nullptr && entry->second.shader->isCompiled()
        };
    }

    std::vector<ShaderID> ShaderManager::getAllShaderIDs() const
    {
        std::scoped_lock lock(m_mutex);
        std::vector<ShaderID> ids;
        ids.reserve(m_entries.size());
        for (const auto& [id, entry] : m_entries)
        {
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    void ShaderManager::enqueueCompile(const ShaderID id)
    {
        const auto entry = m_entries.find(id);
        if (entry == m_entries.end())
            return;
        if (entry->second.status == ShaderStatus::Compiling)
        {
            try
            {
                m_recompilePending.insert(id);
            }
            catch (const std::bad_alloc&)
            {
                LOG_ERROR("[ShaderManager] Failed to queue a pending shader recompilation for ID {}.", id);
            }
            catch (const std::length_error&)
            {
                LOG_ERROR("[ShaderManager] Pending shader recompilation capacity exceeded for ID {}.", id);
            }
            return;
        }
        try
        {
            m_requests.push({ id, entry->second.compileDesc });
        }
        catch (const std::bad_alloc&)
        {
            entry->second.status = ShaderStatus::ReloadFailed;
            LOG_ERROR("[ShaderManager] Failed to allocate a shader compile request for ID {}.", id);
            return;
        }
        catch (const std::length_error&)
        {
            entry->second.status = ShaderStatus::ReloadFailed;
            LOG_ERROR("[ShaderManager] Shader compile request capacity exceeded for ID {}.", id);
            return;
        }
        entry->second.status = ShaderStatus::Compiling;
        m_condition.notify_one();
    }

    void ShaderManager::compileWorker()
    {
        while (m_running)
        {
            CompileRequest request;
            {
                std::unique_lock lock(m_mutex);
                m_condition.wait(lock, [this] { return !m_running || !m_requests.empty(); });
                if (!m_running)
                    return;
                request = std::move(m_requests.front());
                m_requests.pop();
            }
            try
            {
                const ShaderCompileResult result = m_compiler.compile(request.desc);
                std::vector<std::filesystem::path> dependencies;
                if (result.success)
                    dependencies = collectDependencies(request.desc.sourcePath, request.desc.includeDirectories);
                const std::scoped_lock lock(m_mutex);
                m_results.push({ request.id, result, std::move(dependencies) });
            }
            catch (const std::exception& exception)
            {
                const std::scoped_lock lock(m_mutex);
                if (const auto entry = m_entries.find(request.id); entry != m_entries.end())
                    entry->second.status = ShaderStatus::ReloadFailed;
                m_recompilePending.erase(request.id);
                LOG_ERROR("[ShaderManager] Shader compilation worker failed for ID {}: {}", request.id, exception.what());
            }
            catch (...)
            {
                const std::scoped_lock lock(m_mutex);
                if (const auto entry = m_entries.find(request.id); entry != m_entries.end())
                    entry->second.status = ShaderStatus::ReloadFailed;
                m_recompilePending.erase(request.id);
                LOG_ERROR("[ShaderManager] Shader compilation worker failed for ID {} with an unknown exception.", request.id);
            }
        }
    }
} // namespace Engine