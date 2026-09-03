#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Core\Logging\Logging.h"
#include "Core\Serialization\FlatBufferReader.h"

namespace Engine::Serialization
{
    bool FlatBufferReader::open(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            LOG_ERROR("Failed to open FlatBuffers asset: {}", path.string());
            return false;
        }

        const std::streamsize fileSize = file.tellg();
        if (fileSize <= 0) {
            LOG_ERROR("FlatBuffers asset is empty: {}", path.string());
            return false;
        }

        m_data.resize(static_cast<std::size_t>(fileSize));
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(m_data.data()), fileSize)) {
            LOG_ERROR("Failed to read FlatBuffers asset: {}", path.string());
            m_data.clear();
            return false;
        }
        return validate();
    }

    bool FlatBufferReader::validate() const
    {
        return !m_data.empty();
    }

    bool FlatBufferReader::hasIdentifier(const char* identifier) const
    {
        return identifier != nullptr && !m_data.empty() && flatbuffers::BufferHasIdentifier(m_data.data(), identifier);
    }
}