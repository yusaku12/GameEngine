#pragma once

#include <cstdint>
#include <vector>
#include "Core\Math\Geometry.h"

namespace Engine
{
    struct ModelResource;

    /** @brief Applied chunk edge length in grid cells, not vertices or world units. */
    struct TerrainChunkSettings
    {
        std::uint32_t cellsPerChunk = 64;
        friend bool operator==(const TerrainChunkSettings&, const TerrainChunkSettings&) = default;
    };

    /** @brief One row-major chunk; mesh positions and bounds remain in whole-terrain local space. */
    struct TerrainChunk
    {
        std::uint32_t column = 0;
        std::uint32_t row = 0;
        std::uint32_t sampleX = 0;
        std::uint32_t sampleZ = 0;
        std::uint32_t cellsX = 0;
        std::uint32_t cellsZ = 0;
        std::uint32_t meshIndex = 0;
        AABB bounds{};
    };

    /** @brief Validates a chunk edge length of 16..1024 cells; logs invalid settings. */
    bool validateTerrainChunkSettings(const TerrainChunkSettings& settings);

    /**
     * @brief Partitions a complete generated grid after normal/tangent/material-weight calculation.
     * @details Shared border vertices retain identical positions, normals, tangents, UVs and colors.
     * No LOD, streaming or background work is performed. Partial final chunks retain every cell.
     * @param model Whole row-major terrain mesh; replaced only on success.
     * @param width Heightmap sample count in X.
     * @param height Heightmap sample count in Z.
     * @param settings Validated cell edge length.
     * @param chunks Receives row-major metadata whose mesh indices match the replacement model.
     * @return True on success. Invalid inputs are logged without replacing either output.
     * @thread_safety Safe for independent mutable outputs.
     */
    bool partitionTerrainModel(ModelResource& model, std::uint32_t width, std::uint32_t height,
        const TerrainChunkSettings& settings, std::vector<TerrainChunk>& chunks);
}
