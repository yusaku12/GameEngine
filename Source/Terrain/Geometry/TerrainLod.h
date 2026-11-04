#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include "Core\Math\Geometry.h"

namespace Engine
{
    struct ModelResource;
    struct TerrainChunk;

    /** @brief Opt-in distance LOD. Thresholds double per level; collision remains full resolution. */
    struct TerrainLodSettings
    {
        bool enabled = false;
        std::uint32_t maxLevel = 4;
        float distance = 50.0f;
        friend bool operator==(const TerrainLodSettings&, const TerrainLodSettings&) = default;
    };

    /** @brief One triangle index range in a chunk's existing index buffer. */
    struct TerrainLodRange
    {
        std::uint32_t start = 0;
        std::uint32_t count = 0;
    };

    /** @brief Per-chunk LOD ranges; level zero is the original full-resolution mesh. */
    struct TerrainChunkLod
    {
        std::array<TerrainLodRange, 5> levels{};
        std::uint32_t levelCount = 1;
    };

    /** @brief Immutable with a queued model snapshot; never stores camera-dependent state. */
    struct TerrainLodSnapshot
    {
        TerrainLodSettings settings;
        std::vector<TerrainChunkLod> chunks;
    };

    /** @brief Validates maxLevel 1..4 and finite first transition distance 0.1..1000000; logs failures. */
    bool validateTerrainLodSettings(const TerrainLodSettings& settings);

    /**
     * @brief Appends simplified index ranges retaining every full-resolution chunk-border edge.
     * @details Vertex attributes/bounds and the original submesh range are unchanged. Interior
     * samples are decimated with a full-resolution outer ring, preventing cracks at arbitrary
     * adjacent levels without skirts, neighbor state or camera-dependent mesh generation.
     * @param model Generated row-major chunk meshes. Only unused index-buffer tails are appended.
     * @param chunks Matching chunk metadata.
     * @param settings Validated applied settings.
     * @param destination Replaced on success with level ranges matching the generated model.
     * @return True on success; invalid inputs are logged.
     * @thread_safety Independent model/destination pairs may be built concurrently.
     */
    bool buildTerrainLods(ModelResource& model, std::span<const TerrainChunk> chunks,
        const TerrainLodSettings& settings, TerrainLodSnapshot& destination);

    /**
     * @brief Chooses a level from distance to the closest world AABB point, independently per view.
     * @param settings Validated applied settings.
     * @param bounds World-space full-resolution bounds, including all simplified triangles.
     * @param camera Finite camera position.
     * @param levelCount Available levels including level zero (1..5).
     * @return Level zero inside/near bounds or with disabled LOD; coarser levels at doubled distances.
     * @thread_safety Thread-safe for immutable inputs.
     */
    std::uint32_t selectTerrainLod(const TerrainLodSettings& settings, const AABB& bounds,
        const Vector3& camera, std::uint32_t levelCount) noexcept;
}
