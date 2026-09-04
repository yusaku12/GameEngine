#include "Pch.h"
#include "ShaderFileWatcher.h"

namespace Engine
{
    ShaderFileWatcher::~ShaderFileWatcher() { stop(); }

    bool ShaderFileWatcher::start(const std::filesystem::path& directory, const std::chrono::milliseconds debounce)
    {
        if (m_running || directory.empty() || !std::filesystem::is_directory(directory))
            return false;
        m_directory = std::filesystem::absolute(directory);
        m_debounce = debounce;
        m_directoryHandle = CreateFileW(
            m_directory.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (m_directoryHandle == INVALID_HANDLE_VALUE)
        {
            m_directoryHandle = nullptr;
            return false;
        }
        m_running = true;
        m_thread = std::thread(&ShaderFileWatcher::watch, this);
        return true;
    }

    void ShaderFileWatcher::stop()
    {
        if (!m_running.exchange(false))
            return;
        CancelIoEx(static_cast<HANDLE>(m_directoryHandle), nullptr);
        if (m_thread.joinable())
            m_thread.join();
        CloseHandle(static_cast<HANDLE>(m_directoryHandle));
        m_directoryHandle = nullptr;
    }

    std::vector<std::filesystem::path> ShaderFileWatcher::consumeChanges()
    {
        std::scoped_lock lock(m_mutex);
        std::vector<std::filesystem::path> changes;
        changes.swap(m_changes);
        return changes;
    }

    void ShaderFileWatcher::watch()
    {
        std::array<std::byte, 16 * 1024> buffer{};
        while (m_running)
        {
            OVERLAPPED overlapped{};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (overlapped.hEvent == nullptr)
                break;
            DWORD bytesReturned = 0;
            const BOOL requested = ReadDirectoryChangesW(
                static_cast<HANDLE>(m_directoryHandle), buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, &overlapped, nullptr);
            if (!requested || (WaitForSingleObject(overlapped.hEvent, 250) == WAIT_TIMEOUT && !m_running))
            {
                CloseHandle(overlapped.hEvent);
                continue;
            }
            GetOverlappedResult(static_cast<HANDLE>(m_directoryHandle), &overlapped, &bytesReturned, FALSE);
            for (DWORD offset = 0; offset < bytesReturned;)
            {
                const FILE_NOTIFY_INFORMATION* const info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
                const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                const std::filesystem::path path = m_directory / name;
                if (path.extension() == ".hlsl" || path.extension() == ".hlsli")
                {
                    std::scoped_lock lock(m_mutex);
                    if (std::find(m_changes.begin(), m_changes.end(), path) == m_changes.end())
                        m_changes.push_back(path);
                }
                if (info->NextEntryOffset == 0)
                    break;
                offset += info->NextEntryOffset;
            }
            CloseHandle(overlapped.hEvent);
            if (m_debounce.count() > 0)
                std::this_thread::sleep_for(m_debounce);
        }
    }
} // namespace Engine