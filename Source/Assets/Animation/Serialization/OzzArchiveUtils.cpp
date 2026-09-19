#include "Pch.h"
#include "Assets\Animation\Serialization\OzzArchiveUtils.h"
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>

namespace Engine::Serialization
{
    namespace
    {
        template<class T>
        bool save(const T& value, std::vector<std::uint8_t>& bytes)
        {
            ozz::io::MemoryStream stream;
            {
                ozz::io::OArchive archive(&stream, ozz::GetNativeEndianness());
                archive << value;
            }
            bytes.resize(stream.Size());
            return stream.Seek(0, ozz::io::Stream::kSet) == 0
                && stream.Read(bytes.data(), bytes.size()) == bytes.size();
        }

        template<class T>
        bool load(const std::span<const std::uint8_t> bytes, T& value)
        {
            if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return false;
            ozz::io::MemoryStream stream;
            if (stream.Write(bytes.data(), bytes.size()) != bytes.size()
                || stream.Seek(0, ozz::io::Stream::kSet) != 0)
                return false;
            ozz::io::IArchive archive(&stream);
            if (!archive.TestTag<T>())
                return false;
            archive >> value;
            return true;
        }
    }

    std::uint64_t calculateArchiveChecksum(const std::span<const std::uint8_t> bytes) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const std::uint8_t byte : bytes)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool saveOzzArchive(const ozz::animation::Skeleton& skeleton, std::vector<std::uint8_t>& bytes)
    {
        return save(skeleton, bytes);
    }

    bool saveOzzArchive(const ozz::animation::Animation& animation, std::vector<std::uint8_t>& bytes)
    {
        return save(animation, bytes);
    }

    bool loadOzzArchive(const std::span<const std::uint8_t> bytes, ozz::animation::Skeleton& skeleton)
    {
        return load(bytes, skeleton);
    }

    bool loadOzzArchive(const std::span<const std::uint8_t> bytes, ozz::animation::Animation& animation)
    {
        return load(bytes, animation);
    }
}