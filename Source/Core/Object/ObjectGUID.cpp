#include "Pch.h"
#include "Core\Object\ObjectGUID.h"
#include <mutex>

namespace Engine
{
    ObjectGUID ObjectGUID::generate()
    {
        static std::mutex generatorMutex;
        static std::random_device device;
        static std::mt19937_64 generator(device());

        const std::lock_guard lock(generatorMutex);
        ObjectGUID result{ generator(), generator() };
        while (!result.isValid())
            result = { generator(), generator() };
        return result;
    }
} // namespace Engine