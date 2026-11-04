#pragma once

#include <cstddef>
#include <filesystem>

namespace Engine
{
    /**
     * @brief Runs exact surface queries, PhysX terrain integration and persistence regression checks.
     * @param directory Existing empty/new terrain validation output directory.
     * @param passed Receives the number of successful checks.
     * @return True only when all checks pass. Requires initialized PhysicsSystem on the game thread.
     */
    bool validateTerrainCollision(const std::filesystem::path& directory, std::size_t& passed);
}
