#include "Pch.h"
#include "Terrain\TerrainNoise.h"
#include "Terrain\Heightmap.h"
#include <cmath>

namespace Engine
{
    namespace
    {
        constexpr double INVERSE_SQRT_TWO = 0.7071067811865475244;

        std::uint64_t mix(std::uint64_t value) noexcept
        {
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        }

        double gradient(const std::int64_t x, const std::int64_t z, const std::uint32_t seed,
            const double dx, const double dz) noexcept
        {
            const std::uint64_t hash = mix(mix(static_cast<std::uint64_t>(x) + 0x9e3779b97f4a7c15ull)
                ^ mix(static_cast<std::uint64_t>(z) + 0x632be59bd9b4e019ull) ^ mix(seed));
            switch (hash & 7u)
            {
            case 0: return dx;
            case 1: return -dx;
            case 2: return dz;
            case 3: return -dz;
            case 4: return (dx + dz) * INVERSE_SQRT_TWO;
            case 5: return (dx - dz) * INVERSE_SQRT_TWO;
            case 6: return (-dx + dz) * INVERSE_SQRT_TWO;
            default: return (-dx - dz) * INVERSE_SQRT_TWO;
            }
        }

        double fade(const double t) noexcept
        {
            return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
        }

        double perlin(const double x, const double z, const std::uint32_t seed) noexcept
        {
            // Validated settings bound all octave coordinates below 2^39, safely inside int64.
            const double floorX = std::floor(x);
            const double floorZ = std::floor(z);
            const auto ix = static_cast<std::int64_t>(floorX);
            const auto iz = static_cast<std::int64_t>(floorZ);
            const double dx = x - floorX;
            const double dz = z - floorZ;
            const double u = fade(dx);
            const double v = fade(dz);
            const double top = std::lerp(gradient(ix, iz, seed, dx, dz),
                gradient(ix + 1, iz, seed, dx - 1.0, dz), u);
            const double bottom = std::lerp(gradient(ix, iz + 1, seed, dx, dz - 1.0),
                gradient(ix + 1, iz + 1, seed, dx - 1.0, dz - 1.0), u);
            return std::lerp(top, bottom, v);
        }
    }

    bool validateTerrainNoiseSettings(const TerrainNoiseSettings& settings)
    {
        const auto inRange = [](const float value, const float minimum, const float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        };
        if ((settings.mode != TerrainNoiseMode::Perlin && settings.mode != TerrainNoiseMode::FBM)
            || settings.width < 2 || settings.width > Heightmap::MAX_DIMENSION
            || settings.height < 2 || settings.height > Heightmap::MAX_DIMENSION
            || !inRange(settings.frequency, 0.001f, 1024.0f)
            || !inRange(settings.offsetX, -100000.0f, 100000.0f)
            || !inRange(settings.offsetZ, -100000.0f, 100000.0f)
            || settings.octaves < 1 || settings.octaves > 12
            || !inRange(settings.persistence, 0.0f, 1.0f)
            || !inRange(settings.lacunarity, 1.0f, 4.0f))
        {
            LOG_ERROR("[TerrainNoise] Invalid mode, grid dimensions, frequency, offsets or FBM parameters.");
            return false;
        }
        return true;
    }

    bool generateTerrainNoise(const TerrainNoiseSettings& settings, Heightmap& destination, const std::atomic<bool>* cancelled)
    {
        if (!validateTerrainNoiseSettings(settings))
            return false;
        std::vector<float> samples(static_cast<std::size_t>(settings.width) * settings.height);
        const std::uint32_t octaves = settings.mode == TerrainNoiseMode::Perlin ? 1 : settings.octaves;
        for (std::uint32_t z = 0; z < settings.height; ++z)
        {
            if (cancelled && cancelled->load(std::memory_order_acquire))
                return false;
            for (std::uint32_t x = 0; x < settings.width; ++x)
            {
                double sampleX = static_cast<double>(x) / (settings.width - 1) * settings.frequency + settings.offsetX;
                double sampleZ = static_cast<double>(z) / (settings.height - 1) * settings.frequency + settings.offsetZ;
                double amplitude = 1.0;
                double total = 0.0;
                double weight = 0.0;
                for (std::uint32_t octave = 0; octave < octaves && amplitude > 0.0; ++octave)
                {
                    total += amplitude * perlin(sampleX, sampleZ, settings.seed);
                    weight += amplitude;
                    amplitude *= settings.persistence;
                    sampleX *= settings.lacunarity;
                    sampleZ *= settings.lacunarity;
                }
                // Unit-gradient corner dots are bounded by sqrt(2); interpolation is convex.
                const double normalized = 0.5 + 0.5 * INVERSE_SQRT_TWO * total / weight;
                samples[static_cast<std::size_t>(z) * settings.width + x] =
                    static_cast<float>(std::clamp(normalized, 0.0, 1.0));
            }
        }
        return destination.assign(settings.width, settings.height, samples);
    }
}
