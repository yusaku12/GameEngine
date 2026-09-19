#include "Pch.h"
#include "Assets\Animation\AnimatorControllerAsset.h"

namespace Engine
{
    AnimatorStateID makeAnimatorID(const std::string_view name) noexcept
    {
        std::uint32_t hash = 2166136261u;
        for (const unsigned char character : name)
        {
            hash ^= character;
            hash *= 16777619u;
        }
        return hash == 0 ? 1u : hash;
    }
}