#include "Pch.h"
#include "ShaderManager.h"

namespace Engine
{
    namespace
    {
        void collectIncludes(const std::filesystem::path& sourcePath, std::vector<std::filesystem::path>& dependencies)
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
                const auto includePath = std::filesystem::absolute(sourcePath.parent_path() / line.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1));
                if (std::find(dependencies.begin(), dependencies.end(), includePath) == dependencies.end())
                {
                    dependencies.push_back(includePath);
                    collectIncludes(includePath, dependencies);
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
        m_worker = std::thread(&ShaderManager::compileWorker, this);
        if (m_mode != ShaderMode::Runtime && !m_watcher.start(m_shaderRoot))
            LOG_WARNING("[ShaderHotReload] File watcher could not start: {}", m_shaderRoot.string());
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
    }

    ShaderID ShaderManager::registerShader(const ShaderCompileDesc& compileDesc)
    {
        const ShaderID id = m_nextId++;
        ShaderCompileDesc normalized = compileDesc;
        normalized.sourcePath = std::filesystem::absolute(normalized.sourcePath);
        normalized.outputPath = std::filesystem::absolute(normalized.outputPath);
        const std::vector<std::filesystem::path> dependencies = collectDependencies(normalized.sourcePath);
        std::scoped_lock lock(m_mutex);
        m_entries.emplace(id, Entry{ .compileDesc = std::move(normalized), .dependencies = dependencies, .shader = std::make_shared<DX12Shader>() });
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
            std::scoped_lock lock(m_mutex);
            auto entry = m_entries.find(compileResult.id);
            if (entry == m_entries.end())
                continue;
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
            entry->second.status = ShaderStatus::Loaded;
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
        {
            enqueueCompile(id);
        }
    }

    bool ShaderManager::reloadAll()
    {
        std::scoped_lock lock(m_mutex);
        bool allSucceeded = true;
        for (auto& [id, entry] : m_entries)
        {
            auto reloaded = std::make_shared<DX12Shader>();
            if (reloaded->load(DX12ShaderConfig{ .bytecodePath = entry.compileDesc.outputPath }))
            {
                entry.shader = std::move(reloaded);
                entry.status = ShaderStatus::Loaded;
                if (m_callback)
                    m_callback(id);
            }
            else
            {
                entry.status = ShaderStatus::ReloadFailed;
                allSucceeded = false;
            }
        }
        return allSucceeded;
    }

    std::shared_ptr<const DX12Shader> ShaderManager::get(const ShaderID id) const
    {
        std::scoped_lock lock(m_mutex);
        const auto entry = m_entries.find(id);
        return entry == m_entries.end() ? nullptr : entry->second.shader;
    }

    std::vector<std::filesystem::path> ShaderManager::collectDependencies(const std::filesystem::path& sourcePath)
    {
        std::vector<std::filesystem::path> dependencies;
        collectIncludes(sourcePath, dependencies);
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
        entry->second.status = ShaderStatus::Compiling;
        m_requests.push({ id, entry->second.compileDesc });
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
            const ShaderCompileResult result = m_compiler.compile(request.desc);
            {
                std::scoped_lock lock(m_mutex);
                m_results.push({ request.id, result });
            }
        }
    }
} // namespace Engine