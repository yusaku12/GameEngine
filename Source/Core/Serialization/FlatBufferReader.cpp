#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Core\Logging\Logging.h"
#include "Core\Serialization\FlatBufferReader.h"

namespace Engine::Serialization
{
    bool FlatBufferReader::open(const std::filesystem::path& path)
    {
        m_data.clear();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            LOG_ERROR("Failed to open FlatBuffers asset: {}", path.string());
            return false;
        } // namespace Engine::Serialization

        const std::streamsize fileSize = file.tellg();
        if (fileSize <= 0) {
            LOG_ERROR("FlatBuffers asset is empty: {}", path.string());
            return false;
        }

        if (static_cast<std::uintmax_t>(fileSize) > static_cast<std::uintmax_t>(m_data.max_size())) {
            LOG_ERROR("FlatBuffers asset is too large to load: {}", path.string());
            return false;
        }
        try
        {
            m_data.resize(static_cast<std::size_t>(fileSize));
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory for FlatBuffers asset: {}", path.string());
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("FlatBuffers asset exceeds the supported size: {}", path.string());
            return false;
        }

        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(m_data.data()), fileSize)) {
            LOG_ERROR("Failed to read FlatBuffers asset: {}", path.string());
            m_data.clear();
            return false;
        }
        if (!validate())
        {
            m_data.clear();
            return false;
        }
        return true;
    }

    bool FlatBufferReader::validate() const
    {
        return !m_data.empty();
    }

    bool FlatBufferReader::hasIdentifier(const char* identifier) const
    {
        constexpr std::size_t minimumSize =
            sizeof(flatbuffers::uoffset_t) + flatbuffers::kFileIdentifierLength;
        if (identifier == nullptr || m_data.size() < minimumSize)
        {
            LOG_ERROR("FlatBuffers asset is too small to contain an identifier.");
            return false;
        }

        return flatbuffers::BufferHasIdentifier(m_data.data(), identifier);
    }
}