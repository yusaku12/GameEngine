#pragma once

#include <cstdint>
#include <atomic>

namespace Engine
{
    class Heightmap;

    /** @brief Single-octave gradient noise or normalized fractal Brownian motion. */
    enum class TerrainNoiseMode : std::uint8_t
    {
        Perlin,
        FBM
    };

    /** @brief Reproducible CPU terrain recipe; independent of mesh dimensions and global RNG state. */
    struct TerrainNoiseSettings
    {
        TerrainNoiseMode mode = TerrainNoiseMode::FBM;
        std::uint32_t seed = 0;
        std::uint32_t width = 65;
        std::uint32_t height = 65;
        float frequency = 4.0f;
        float offsetX = 0.0f;
        float offsetZ = 0.0f;
        std::uint32_t octaves = 6;
        float persistence = 0.5f;
        float lacunarity = 2.0f;

        /** @brief Compares all recipe fields, including single-octave inactive FBM parameters. */
        bool operator==(const TerrainNoiseSettings&) const = default;
    };

    /**
     * @brief Validates a recipe without allocating sample storage; logs invalid input.
     * @param settings Grid: 2..Heightmap::MAX_DIMENSION per axis; frequency: 0.001..1024;
     * offsets: -100000..100000; octaves: 1..12; persistence: 0..1; lacunarity: 1..4.
     * @return True only for a valid mode and finite, bounded fields.
     */
    bool validateTerrainNoiseSettings(const TerrainNoiseSettings& settings);

    /**
     * @brief Generates seeded 2D Perlin noise or amplitude-normalized FBM into [0, 1] heights.
     * @details Uses quintic interpolation and fixed unit gradients. Base coordinates are
     * (x / (width - 1) * frequency + offsetX, z / (height - 1) * frequency + offsetZ).
     * FBM scales coordinates by lacunarity and amplitude by persistence each octave.
     * Output uses a fixed theoretical bound, not per-map min/max normalization.
     * Identical recipes reproduce identical samples in the same build.
     * @param settings Validated generation recipe; Perlin evaluates exactly one octave.
     * @param destination Replaced only on success. Invalid input is logged.
     * @param cancelled Optional cancellation flag, checked each sample row.
     * @return True when assigned successfully; false on logged invalid input or explicit cancellation.
     * @thread_safety Thread-safe for independent destinations; no shared mutable state.
     */
    bool generateTerrainNoise(const TerrainNoiseSettings& settings, Heightmap& destination,
        const std::atomic<bool>* cancelled = nullptr);
}
