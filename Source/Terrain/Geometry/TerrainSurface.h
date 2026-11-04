#pragma once

#include <optional>
#include "Core\Math\MathTypes.h"

namespace Engine
{
    class Heightmap;
    struct TerrainMeshSettings;

    /** @brief Position and geometric triangle normal, in the requested coordinate space. */
    struct TerrainSurface
    {
        Vector3 position;
        Vector3 normal;
    };

    /**
     * @brief Samples the exact a/c/b, b/c/d triangles used by the terrain mesh (not bilinear heights).
     * @param heights Valid normalized height data.
     * @param settings Valid applied mesh dimensions.
     * @param x Local X within the centered grid, including its boundary.
     * @param z Local Z within the centered grid, including its boundary.
     * @return Surface or nullopt outside the grid. Nonfinite coordinates are logged.
     * @thread_safety Concurrent reads are safe if inputs are not modified.
     */
    std::optional<TerrainSurface> sampleTerrainSurface(const Heightmap& heights,
        const TerrainMeshSettings& settings, float x, float z) noexcept;

    /**
     * @brief Finds the highest world-space intersection with a vertical line at world XZ.
     * @details Traverses projected grid cells without allocating; supports invertible affine transforms.
     * The input Y is ignored. A miss is normal; invalid coordinates/transforms are logged.
     * @param heights Valid normalized height data.
     * @param settings Valid applied mesh dimensions.
     * @param world Terrain local-to-world matrix.
     * @param position Query world XZ.
     * @return Highest surface, with upward-facing geometric normal, or nullopt.
     * @thread_safety Concurrent reads are safe if inputs are not modified.
     */
    std::optional<TerrainSurface> sampleTerrainSurfaceWorld(const Heightmap& heights,
        const TerrainMeshSettings& settings, const Matrix& world, const Vector3& position) noexcept;
}
