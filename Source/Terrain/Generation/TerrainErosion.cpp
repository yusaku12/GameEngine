#include "Pch.h"
#include "Terrain\Generation\TerrainErosion.h"
#include "Terrain\Data\Heightmap.h"
#include <cmath>
#include <random>

namespace Engine
{
    namespace
    {
        struct BrushPoint { int x, z; double weight; };
        struct Surface
        {
            std::array<std::size_t, 4> indices;
            std::array<double, 4> weights;
            double height, gradientX, gradientZ;
        };

        Surface sample(const std::vector<double>& heights, const std::uint32_t width, const double x, const double z)
        {
            const auto ix = static_cast<std::uint32_t>(x);
            const auto iz = static_cast<std::uint32_t>(z);
            const double u = x - ix, v = z - iz;
            const std::size_t a = static_cast<std::size_t>(iz) * width + ix;
            const double h00 = heights[a], h10 = heights[a + 1], h01 = heights[a + width], h11 = heights[a + width + 1];
            return { { a, a + 1, a + width, a + width + 1 },
                { (1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v },
                std::lerp(std::lerp(h00, h10, u), std::lerp(h01, h11, u), v),
                std::lerp(h10 - h00, h11 - h01, v), std::lerp(h01 - h00, h11 - h10, u) };
        }

        double deposit(std::vector<double>& heights, const Surface& surface, const double amount)
        {
            double deposited = 0;
            for (std::size_t i = 0; i < surface.indices.size(); ++i)
            {
                auto& height = heights[surface.indices[i]];
                const double change = std::min(amount * surface.weights[i], 1 - height);
                height += change;
                deposited += change;
            }
            return deposited;
        }

        double erode(std::vector<double>& heights, const std::uint32_t width, const std::uint32_t height,
            const double x, const double z, const std::vector<BrushPoint>& brush, const double amount)
        {
            const int centerX = static_cast<int>(std::round(x)), centerZ = static_cast<int>(std::round(z));
            const auto inside = [&](const BrushPoint& point)
            {
                return centerX + point.x >= 0 && centerX + point.x < static_cast<int>(width)
                    && centerZ + point.z >= 0 && centerZ + point.z < static_cast<int>(height);
            };
            double weight = 0;
            for (const auto& point : brush)
                if (inside(point))
                    weight += point.weight;
            double removed = 0;
            for (const auto& point : brush)
            {
                if (!inside(point))
                    continue;
                auto& value = heights[static_cast<std::size_t>(centerZ + point.z) * width + centerX + point.x];
                const double change = std::min(value, amount * point.weight / weight);
                value -= change;
                removed += change;
            }
            return removed;
        }
    }

    bool validateTerrainErosionSettings(const TerrainErosionSettings& settings)
    {
        const auto inRange = [](const float value, const float minimum, const float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        };
        if (settings.droplets < 1 || settings.droplets > 1000000
            || settings.maxLifetime < 1 || settings.maxLifetime > 256 || settings.radius < 1 || settings.radius > 8
            || !inRange(settings.inertia, 0, 1) || !inRange(settings.capacity, 0.01f, 100)
            || !inRange(settings.minSlope, 0.00001f, 1) || !inRange(settings.depositRate, 0, 1)
            || !inRange(settings.erosionRate, 0, 1) || !inRange(settings.evaporation, 0, 1)
            || !inRange(settings.gravity, 0, 100) || !inRange(settings.initialWater, 0.01f, 10)
            || !inRange(settings.initialSpeed, 0, 10))
        {
            LOG_ERROR("[TerrainErosion] Invalid droplet count, lifetime, radius or finite bounded water/sediment parameters.");
            return false;
        }
        return true;
    }

    bool erodeTerrain(const Heightmap& source, const TerrainErosionSettings& settings, Heightmap& destination,
        const std::atomic<bool>* cancelled, TerrainErosionBudget* budget)
    {
        if (!validateTerrainErosionSettings(settings))
            return false;
        const auto width = source.width(), height = source.height();
        if (width < 2 || width > Heightmap::MAX_DIMENSION || height < 2 || height > Heightmap::MAX_DIMENSION
            || source.samples().size() != static_cast<std::size_t>(width) * height)
        {
            LOG_ERROR("[TerrainErosion] Invalid or moved-from source heightmap.");
            return false;
        }
        const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (stopped())
            return false;
        std::vector<double> heights(source.samples().begin(), source.samples().end());
        std::vector<BrushPoint> brush;
        const int radius = static_cast<int>(settings.radius);
        for (int z = -radius + 1; z < radius; ++z)
            for (int x = -radius + 1; x < radius; ++x)
            {
                const double distance = std::sqrt(static_cast<double>(x * x + z * z));
                if (distance < radius)
                    brush.push_back({ x, z, radius - distance });
            }
        std::mt19937 random(settings.seed);
        // Explicit conversion avoids implementation-dependent uniform_real_distribution behavior.
        const auto unit = [&] { return static_cast<double>(random()) / 4294967296.0; };
        TerrainErosionBudget totals;
        for (std::uint32_t droplet = 0; droplet < settings.droplets; ++droplet)
        {
            if (stopped())
                return false;
            double x = unit() * (width - 1), z = unit() * (height - 1);
            double directionX = 0, directionZ = 0, speed = settings.initialSpeed, water = settings.initialWater, sediment = 0;
            bool exited = false;
            for (std::uint32_t step = 0; step < settings.maxLifetime; ++step)
            {
                if (stopped())
                    return false;
                const auto current = sample(heights, width, x, z);
                directionX = directionX * settings.inertia - current.gradientX * (1 - settings.inertia);
                directionZ = directionZ * settings.inertia - current.gradientZ * (1 - settings.inertia);
                const double length = std::hypot(directionX, directionZ);
                if (length < 1e-12 || water < 1e-6)
                    break;
                directionX /= length;
                directionZ /= length;
                const double nextX = x + directionX, nextZ = z + directionZ;
                if (nextX < 0 || nextX >= width - 1 || nextZ < 0 || nextZ >= height - 1)
                {
                    exited = true;
                    break;
                }
                const double delta = sample(heights, width, nextX, nextZ).height - current.height;
                const double capacity = std::max(-delta, static_cast<double>(settings.minSlope)) * speed * water * settings.capacity;
                if (delta > 0 || sediment > capacity)
                {
                    const double amount = delta > 0 ? std::min(delta, sediment)
                        : (sediment - capacity) * settings.depositRate;
                    const double deposited = deposit(heights, current, amount);
                    sediment = std::max(0.0, sediment - deposited);
                    totals.deposited += deposited;
                }
                else
                {
                    const double amount = std::min((capacity - sediment) * settings.erosionRate, -delta);
                    const double removed = erode(heights, width, height, x, z, brush, amount);
                    sediment += removed;
                    totals.eroded += removed;
                }
                speed = std::sqrt(std::max(0.0, speed * speed - delta * settings.gravity));
                water *= 1 - settings.evaporation;
                x = nextX;
                z = nextZ;
            }
            if (!exited)
            {
                const double deposited = deposit(heights, sample(heights, width, x, z), sediment);
                sediment = std::max(0.0, sediment - deposited);
                totals.deposited += deposited;
            }
            totals.exported += sediment;
        }
        std::vector<float> samples;
        samples.reserve(heights.size());
        for (const double value : heights)
            samples.push_back(static_cast<float>(value));
        if (stopped() || !destination.assign(width, height, samples))
            return false;
        if (budget)
            *budget = totals;
        return true;
    }
}
