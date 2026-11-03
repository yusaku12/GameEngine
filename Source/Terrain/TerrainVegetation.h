#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include "Core\Math\MathTypes.h"

namespace Engine
{
    class Heightmap;
    struct TerrainMeshSettings;
    inline constexpr std::size_t TERRAIN_VEGETATION_LAYERS = 4;
    inline constexpr std::uint32_t TERRAIN_VEGETATION_MAX_CANDIDATES = 100000;

    /** @brief One seeded static-model scatter recipe, measured in terrain-local units. */
    struct TerrainVegetationLayer
    {
        bool enabled = false;
        std::filesystem::path modelPath;
        std::uint32_t seed = 0;
        float density = 0.01f;
        float minimumSpacing = 1;
        std::uint32_t maxInstances = 5000;
        float minimumHeight = 0, maximumHeight = 1;
        float minimumSlope = 0, maximumSlope = 45;
        float minimumScale = 0.8f, maximumScale = 1.2f;
        float heightOffset = 0;
        bool alignToNormal = false;
        bool castShadows = true;
        /** @brief Compares all applied/draft fields, including model reference. */
        bool operator==(const TerrainVegetationLayer&) const = default;
    };

    /** @brief Opt-in four-layer vegetation. Spacing is enforced independently within each layer. */
    struct TerrainVegetationSettings
    {
        bool enabled = false;
        std::array<TerrainVegetationLayer, TERRAIN_VEGETATION_LAYERS> layers;
        /** @brief Compares complete authoring state. */
        bool operator==(const TerrainVegetationSettings&) const = default;
    };

    /** @brief Immutable local model transforms; references remain valid across terrain replacement. */
    using TerrainVegetationPlacement = std::array<std::shared_ptr<const std::vector<Matrix>>, TERRAIN_VEGETATION_LAYERS>;

    /**
     * @brief Validates all recipe fields, including disabled ones; enabled layers require .model/.mdl references.
     * @param settings Density 0..100 per local XZ area, spacing 0.1..10000, maxInstances 1..25000,
     * normalized height 0..1, slope 0..90 degrees, scale 0.001..1000, offset -1000..1000.
     * @param mesh Local dimensions used to enforce the per-layer candidate budget.
     * @return True for valid settings and candidate counts; logs invalid data.
     */
    bool validateTerrainVegetation(const TerrainVegetationSettings& settings, const TerrainMeshSettings& mesh);
    /**
     * @brief Deterministically scatters static models on exact terrain triangles without accessing asset managers.
     * @details Tests ceil(sizeX*sizeZ*density) independent seeded candidates, at most 100000 per layer.
     * Exceeding that budget fails explicitly; accepted instances stop at maxInstances. Height/slope filters
     * and minimum XZ center spacing can reduce actual counts. No cross-layer exclusion is performed.
     * Positive uniform scale, random full yaw and optional normal alignment use +Y-up models with origins at roots.
     * @param heights Valid authoritative samples; moved-from inputs are rejected.
     * @param mesh Applied local terrain dimensions.
     * @param settings Captured recipes. Object/parent transforms and chunk/LOD choices do not affect placement.
     * @param destination Replaced only on success.
     * @param cancelled Optional flag checked each candidate.
     * @return False on logged invalid input or cancellation; destination remains unchanged.
     * @thread_safety Independent destinations are thread-safe; no global RNG state.
     */
    bool buildTerrainVegetation(const Heightmap& heights, const TerrainMeshSettings& mesh,
        const TerrainVegetationSettings& settings, TerrainVegetationPlacement& destination,
        const std::atomic<bool>* cancelled = nullptr);
}
