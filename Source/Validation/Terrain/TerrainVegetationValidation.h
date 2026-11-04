#pragma once

#include <cstddef>
#include <filesystem>

namespace Engine
{
    class DX12Renderer;
    /** @brief Validates seeded scatter, bounds/spacing, persistence, asynchronous lifetime and real GPU instances. */
    bool validateTerrainVegetationFeature(DX12Renderer& renderer, const std::filesystem::path& directory, std::size_t& count);
}
