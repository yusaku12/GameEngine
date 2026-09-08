#include "Pch.h"
#include "Core\Object\ObjectGUID.h"

#include <random>

namespace Engine
{
    ObjectGUID ObjectGUID::generate() noexcept
    {
        static std::random_device device;
        static std::mt19937_64 generator(device());
        ObjectGUID result{ generator(), generator() };
        while (!result.isValid())
            result = { generator(), generator() };
        return result;
    }
} // namespace Engine