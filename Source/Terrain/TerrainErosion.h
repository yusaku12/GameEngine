#pragma once

#include <atomic>
#include <cstdint>

namespace Engine
{
    class Heightmap;

    /** @brief Seeded droplet recipe in grid-cell horizontal units and normalized height units. */
    struct TerrainErosionSettings
    {
        std::uint32_t seed = 0;
        std::uint32_t droplets = 5000;
        std::uint32_t maxLifetime = 64;
        std::uint32_t radius = 3;
        float inertia = 0.05f;
        float capacity = 4.0f;
        float minSlope = 0.001f;
        float depositRate = 0.3f;
        float erosionRate = 0.3f;
        float evaporation = 0.01f;
        float gravity = 4.0f;
        float initialWater = 1.0f;
        float initialSpeed = 1.0f;

        /** @brief Compares all authoring fields. */
        bool operator==(const TerrainErosionSettings&) const = default;
    };

    /** @brief Sediment budget in sums of normalized vertex heights, before final float rounding. */
    struct TerrainErosionBudget
    {
        double eroded = 0;
        double deposited = 0;
        double exported = 0;
    };

    /** @brief Validates finite bounded parameters without allocation; logs invalid recipes. */
    bool validateTerrainErosionSettings(const TerrainErosionSettings& settings);
    /**
     * @brief Applies deterministic CPU hydraulic erosion to a private copy of normalized samples.
     * @details Bilinear flow, radius-weighted erosion and bounded bilinear deposition never
     * renormalize the map. Edges are open: carried sediment exits there. Other droplet stops
     * settle sediment locally; sediment exceeding local [0,1] capacity is exported.
     * Mesh dimensions/transforms do not affect the simulation. Identical inputs reproduce
     * identical outputs in the same build. Source and destination may alias.
     * @param source Valid 2..1025 samples per axis, including rectangular and two-sample grids.
     * @param settings Droplets 1..1000000, lifetime 1..256, radius 1..8; rates/inertia 0..1,
     * capacity 0.01..100, minSlope 0.00001..1, gravity 0..100, water 0.01..10, speed 0..10.
     * @param destination Replaced only on success; failure preserves its previous contents.
     * @param cancelled Optional flag checked every droplet and simulation step.
     * @param budget Optional successful sediment totals; unchanged on failure/cancellation.
     * @return True on success, false on logged invalid input or explicit cancellation.
     * @thread_safety Independent destinations/budgets are thread-safe; no global RNG state.
     */
    bool erodeTerrain(const Heightmap& source, const TerrainErosionSettings& settings, Heightmap& destination,
        const std::atomic<bool>* cancelled = nullptr, TerrainErosionBudget* budget = nullptr);
}
