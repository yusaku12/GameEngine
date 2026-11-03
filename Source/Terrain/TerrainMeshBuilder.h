#pragma once

namespace Engine
{
    class Heightmap;
    struct ModelResource;

    /** @brief Local-space terrain dimensions and UV tiling. */
    struct TerrainMeshSettings
    {
        float sizeX = 100.0f;
        float sizeZ = 100.0f;
        float heightScale = 20.0f;
        float heightOffset = 0.0f;
        float uvRepeatX = 1.0f;
        float uvRepeatZ = 1.0f;
    };

    /**
     * @brief Validates finite, bounded mesh settings and logs invalid input.
     * @param settings Local-space dimensions and UV tiling.
     * @return True for sizes [0.01, 100000], scale [0, 100000], offset [-100000, 100000]
     * and UV repeats [0.001, 10000].
     */
    bool validateTerrainMeshSettings(const TerrainMeshSettings& settings);

    /**
     * @brief Builds a centered XZ grid, +Y-facing triangles and an orthonormal tangent frame.
     * @param heightmap Valid normalized samples. Height is offset + sample * scale.
     * @param settings Physical dimensions and UV tiling; no implicit image resampling.
     * @param destination Replaced only on success with one mesh, one submesh and identity node.
     * @return True on success. Invalid settings are logged.
     * @thread_safety Thread-safe for independent destinations and immutable inputs.
     */
    bool buildTerrainModel(const Heightmap& heightmap, const TerrainMeshSettings& settings,
        ModelResource& destination);
}
