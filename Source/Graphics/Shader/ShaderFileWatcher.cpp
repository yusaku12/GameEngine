#include "Pch.h"
#include "ShaderFileWatcher.h"

namespace Engine
{
    ShaderFileWatcher::~ShaderFileWatcher() { stop(); }

    bool ShaderFileWatcher::start(const std::filesystem::path& directory, const std::chrono::milliseconds debounce)
    {
        if (m_running || directory.empty() || debounce.count() < 0)
            return false;

        stop();
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error))
        {
            if (error)
                LOG_ERROR("[ShaderHotReload] Could not inspect watcher directory {}: {}",
                    directory.string(), error.message());
            else
                LOG_ERROR("[ShaderHotReload] Watcher directory does not exist: {}", directory.string());
            return false;
        }

        m_directory = std::filesystem::absolute(directory, error);
        if (error)
        {
            LOG_ERROR("[ShaderHotReload] Could not resolve watcher directory {}: {}",
                directory.string(), error.message());
            return false;
        }

        m_debounce = debounce;
        m_directoryHandle = CreateFileW(
            m_directory.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (m_directoryHandle == INVALID_HANDLE_VALUE)
        {
            LOG_ERROR("[ShaderHotReload] Could not open watcher directory {} (Windows error: {}).",
                m_directory.string(), GetLastError());
            m_directoryHandle = nullptr;
            return false;
        }

        try
        {
            m_running.store(true, std::memory_order_release);
            m_thread = std::thread(&ShaderFileWatcher::watch, this);
        }
        catch (const std::system_error& exception)
        {
            m_running.store(false, std::memory_order_release);
            CloseHandle(static_cast<HANDLE>(m_directoryHandle));
            m_directoryHandle = nullptr;
            LOG_ERROR("[ShaderHotReload] Could not start watcher thread: {}", exception.what());
            return false;
        }

        return true;
    }

    void ShaderFileWatcher::stop()
    {
        m_running.store(false, std::memory_order_release);
        if (m_directoryHandle != nullptr)
            CancelIoEx(static_cast<HANDLE>(m_directoryHandle), nullptr);
        if (m_thread.joinable())
            m_thread.join();
        if (m_directoryHandle != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_directoryHandle));
            m_directoryHandle = nullptr;
        }
    }

    std::vector<std::filesystem::path> ShaderFileWatcher::consumeChanges()
    {
        std::scoped_lock lock(m_mutex);
        std::vector<std::filesystem::path> changes;
        changes.swap(m_changes);
        return changes;
    }

    bool ShaderFileWatcher::consumeOverflow()
    {
        std::scoped_lock lock(m_mutex);
        return std::exchange(m_overflowed, false);
    }

    void ShaderFileWatcher::watch()
    {
        alignas(FILE_NOTIFY_INFORMATION) std::array<std::byte, 16 * 1024> buffer{};
        const auto markOverflow = [this]
            {
                const std::scoped_lock lock(m_mutex);
                m_overflowed = true;
            };
        while (m_running)
        {
            OVERLAPPED overlapped{};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (overlapped.hEvent == nullptr)
            {
                LOG_ERROR("[ShaderHotReload] Failed to create file watcher event (Windows error: {}).", GetLastError());
                break;
            }

            DWORD bytesReturned = 0;
            const BOOL requested = ReadDirectoryChangesW(
                static_cast<HANDLE>(m_directoryHandle), buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, &overlapped, nullptr);
            const DWORD requestError = requested ? ERROR_SUCCESS : GetLastError();
            if (!requested && requestError != ERROR_IO_PENDING)
            {
                CloseHandle(overlapped.hEvent);
                if (m_running)
                    LOG_ERROR("[ShaderHotReload] Failed to request directory changes (Windows error: {}).", requestError);
                break;
            }

            DWORD waitResult = WAIT_TIMEOUT;
            while (waitResult == WAIT_TIMEOUT)
            {
                waitResult = WaitForSingleObject(overlapped.hEvent, 250);
                if (waitResult == WAIT_TIMEOUT && !m_running)
                {
                    CancelIoEx(static_cast<HANDLE>(m_directoryHandle), &overlapped);
                    waitResult = WaitForSingleObject(overlapped.hEvent, INFINITE);
                }
            }

            if (waitResult != WAIT_OBJECT_0)
            {
                const DWORD error = GetLastError();
                CancelIoEx(static_cast<HANDLE>(m_directoryHandle), &overlapped);
                WaitForSingleObject(overlapped.hEvent, INFINITE);
                CloseHandle(overlapped.hEvent);
                LOG_ERROR("[ShaderHotReload] Waiting for directory changes failed (Windows error: {}).", error);
                break;
            }

            const BOOL completed = GetOverlappedResult(
                static_cast<HANDLE>(m_directoryHandle), &overlapped, &bytesReturned, FALSE);
            const DWORD completionError = completed ? ERROR_SUCCESS : GetLastError();
            if (!completed && m_running && completionError == ERROR_NOTIFY_ENUM_DIR)
            {
                markOverflow();
                LOG_WARNING("[ShaderHotReload] Directory notification buffer overflowed; all shaders must be recompiled.");
            }
            else if (!completed && m_running && completionError != ERROR_OPERATION_ABORTED)
            {
                LOG_ERROR("[ShaderHotReload] Directory change request failed (Windows error: {}).", completionError);
            }

            if (completed && m_running)
            {
                try
                {
                    bool malformedNotification = false;
                    for (DWORD offset = 0; offset < bytesReturned;)
                    {
                        constexpr DWORD fileNameOffset = static_cast<DWORD>(offsetof(FILE_NOTIFY_INFORMATION, FileName));
                        const DWORD remaining = bytesReturned - offset;
                        if (remaining < fileNameOffset + sizeof(wchar_t))
                        {
                            malformedNotification = true;
                            break;
                        }

                        const FILE_NOTIFY_INFORMATION* const info =
                            reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
                        if (info->FileNameLength % sizeof(wchar_t) != 0
                            || info->FileNameLength > remaining - fileNameOffset)
                        {
                            malformedNotification = true;
                            break;
                        }
                        const DWORD entrySize = fileNameOffset + info->FileNameLength;
                        if (info->NextEntryOffset != 0
                            && (info->NextEntryOffset < entrySize
                                || info->NextEntryOffset > remaining
                                || info->NextEntryOffset % alignof(FILE_NOTIFY_INFORMATION) != 0))
                        {
                            malformedNotification = true;
                            break;
                        }

                        const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                        const std::filesystem::path path = m_directory / name;
                        std::wstring extension = path.extension().wstring();
                        std::transform(extension.begin(), extension.end(), extension.begin(),
                            [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
                        if (extension == L".hlsl" || extension == L".hlsli")
                        {
                            const std::scoped_lock lock(m_mutex);
                            if (std::find(m_changes.begin(), m_changes.end(), path) == m_changes.end())
                                m_changes.push_back(path);
                        }
                        if (info->NextEntryOffset == 0)
                            break;
                        offset += info->NextEntryOffset;
                    }
                    if (malformedNotification)
                    {
                        markOverflow();
                        LOG_ERROR("[ShaderHotReload] Received a malformed directory notification; all shaders must be recompiled.");
                    }
                }
                catch (const std::bad_alloc&)
                {
                    markOverflow();
                    LOG_ERROR("[ShaderHotReload] Failed to allocate memory while processing file changes; all shaders must be recompiled.");
                }
                catch (const std::length_error&)
                {
                    markOverflow();
                    LOG_ERROR("[ShaderHotReload] File change list exceeded its capacity; all shaders must be recompiled.");
                }
                catch (const std::filesystem::filesystem_error& exception)
                {
                    markOverflow();
                    LOG_ERROR("[ShaderHotReload] Failed to process a changed path: {}", exception.what());
                }
                catch (const std::exception& exception)
                {
                    markOverflow();
                    LOG_ERROR("[ShaderHotReload] Failed to process file changes: {}", exception.what());
                }
                catch (...)
                {
                    markOverflow();
                    LOG_ERROR("[ShaderHotReload] An unknown error occurred while processing file changes.");
                }
            }
            CloseHandle(overlapped.hEvent);
            if (completed && m_running && m_debounce.count() > 0)
                std::this_thread::sleep_for(m_debounce);
        }
        m_running.store(false, std::memory_order_release);
    }
} // namespace Engine