#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Core\Serialization\FlatBufferWriter.h"

namespace Engine::Serialization
{
    bool FlatBufferWriter::save(const std::filesystem::path& path, std::span<const std::uint8_t> data) const
    {
        if (path.empty() || data.empty()) {
            LOG_ERROR("Cannot save an empty FlatBuffers asset: {}", path.string());
            return false;
        }
        if (data.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)())) {
            LOG_ERROR("FlatBuffers asset is too large to write: {}", path.string());
            return false;
        }

        std::error_code error;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            LOG_ERROR("Failed to create asset directory {}: {}", path.parent_path().string(), error.message());
            return false;
        }

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
            LOG_ERROR("Failed to write FlatBuffers asset: {}", path.string());
            return false;
        }
        file.flush();
        if (!file)
        {
            LOG_ERROR("Failed to flush FlatBuffers asset: {}", path.string());
            return false;
        }
        file.close();
        if (!file)
        {
            LOG_ERROR("Failed to close FlatBuffers asset: {}", path.string());
            return false;
        }
        return true;
    }

    bool FlatBufferWriter::saveAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> data) const
    {
        if (path.empty())
        {
            LOG_ERROR("Cannot atomically save a FlatBuffers asset to an empty path.");
            return false;
        }

        static std::atomic<std::uint64_t> temporaryFileSequence = 0;
        std::filesystem::path temporaryPath = path;
        temporaryPath += L".tmp."
            + std::to_wstring(GetCurrentProcessId())
            + L"."
            + std::to_wstring(temporaryFileSequence.fetch_add(1, std::memory_order_relaxed));
        if (!save(temporaryPath, data))
        {
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            if (cleanupError)
                LOG_ERROR("Failed to remove incomplete temporary asset {}: {}",
                    temporaryPath.string(), cleanupError.message());
            return false;
        }

        if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            if (cleanupError)
                LOG_ERROR("Failed to remove temporary asset {}: {}", temporaryPath.string(), cleanupError.message());
            LOG_ERROR("Failed to replace FlatBuffers asset {} (Windows error: {})", path.string(), error);
            return false;
        }
        return true;
    }
}