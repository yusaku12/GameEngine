#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Core\Serialization\FlatBufferWriter.h"

namespace Engine::Serialization
{
    bool FlatBufferWriter::save(const std::filesystem::path& path, std::span<const std::uint8_t> data) const
    {
        if (data.empty()) {
            LOG_ERROR("Cannot save an empty FlatBuffers asset: {}", path.string());
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
        return true;
    }

    bool FlatBufferWriter::saveAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> data) const
    {
        const std::filesystem::path temporaryPath = path.wstring() + L".tmp";
        if (!save(temporaryPath, data))
            return false;

        std::error_code error;
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
        if (error) {
            LOG_ERROR("Failed to replace FlatBuffers asset {}: {}", path.string(), error.message());
            std::filesystem::remove(temporaryPath, error);
            return false;
        }
        return true;
    }
}