#pragma once

#include <cstddef>
#include <filesystem>

namespace Engine
{
    /**
     * @brief Validates erosion budgets, bounds, reproducibility, persistence and transactional PhysX replacement.
     * @param directory Existing output directory for terrain fixtures and measurements.
     * @param passed Receives successful check count.
     * @return True only after all checks pass.
     */
    bool validateTerrainErosion(const std::filesystem::path& directory, std::size_t& passed);
}
