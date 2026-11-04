#pragma once

#include <cstddef>
#include <filesystem>

namespace Engine
{
    /**
     * @brief Validates actual Compute Shader/readback numerical bounds, reproducibility, lifecycle and physics.
     * @param directory Existing output directory for measurements and persistence fixtures.
     * @param passed Receives successful check count.
     * @return True only when all checks pass on the initialized renderer device.
     */
    bool validateTerrainGpu(const std::filesystem::path& directory, std::size_t& passed);
}
