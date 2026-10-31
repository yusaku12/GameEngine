#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <flatbuffers/flatbuffers.h>

namespace Engine::PhysicsSerialization
{
    template<typename Payload>
    bool readPayload(
        const std::span<const std::uint8_t> bytes,
        const char* const identifier,
        const Payload*& payload) noexcept
    {
        constexpr std::size_t MINIMUM_BUFFER_SIZE =
            sizeof(flatbuffers::uoffset_t) + flatbuffers::kFileIdentifierLength;
        if (bytes.size() < MINIMUM_BUFFER_SIZE
            || !flatbuffers::BufferHasIdentifier(bytes.data(), identifier))
            return false;

        flatbuffers::Verifier verifier(bytes.data(), bytes.size());
        if (!verifier.VerifyBuffer<Payload>(identifier))
            return false;

        payload = flatbuffers::GetRoot<Payload>(bytes.data());
        return payload != nullptr;
    }

    template<typename Payload>
    void writePayload(
        flatbuffers::FlatBufferBuilder& builder,
        const flatbuffers::Offset<Payload> root,
        const char* const identifier,
        std::vector<std::uint8_t>& bytes)
    {
        builder.Finish(root, identifier);
        bytes.assign(builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize());
    }
} // namespace Engine::PhysicsSerialization
