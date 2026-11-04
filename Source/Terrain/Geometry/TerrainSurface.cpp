#include "Pch.h"
#include "Terrain\Geometry\TerrainSurface.h"
#include "Terrain\Data\Heightmap.h"
#include "Terrain\Geometry\TerrainMeshBuilder.h"
#include <cmath>
#include <limits>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\Transform.h"

namespace Engine
{
    namespace
    {
        bool finite(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        float gridPosition(const std::uint32_t index, const std::uint32_t count, const float size) noexcept
        {
            return (static_cast<float>(index) / static_cast<float>(count - 1) - 0.5f) * size;
        }

        std::uint32_t gridCell(const double position, const std::uint32_t count, const float size) noexcept
        {
            const double grid = std::clamp((position / size + 0.5) * (count - 1), 0.0, static_cast<double>(count - 1));
            auto cell = std::min(static_cast<std::uint32_t>(grid), count - 2);
            // Correct the idealized cell at float-rounded mesh boundaries.
            if (cell > 0 && position < gridPosition(cell, count, size))
                --cell;
            else if (cell < count - 2 && position >= gridPosition(cell + 1, count, size))
                ++cell;
            return cell;
        }

        Vector3 vertex(const Heightmap& heights, const TerrainMeshSettings& settings,
            const std::uint32_t x, const std::uint32_t z) noexcept
        {
            return Vector3(gridPosition(x, heights.width(), settings.sizeX),
                settings.heightOffset + heights.samples()[static_cast<std::size_t>(z) * heights.width() + x] * settings.heightScale,
                gridPosition(z, heights.height(), settings.sizeZ));
        }

        Vector3 normal(const Vector3& a, const Vector3& b, const Vector3& c) noexcept
        {
            const auto u = b - a;
            const auto v = c - a;
            const double x = static_cast<double>(u.y) * v.z - static_cast<double>(u.z) * v.y;
            const double y = static_cast<double>(u.z) * v.x - static_cast<double>(u.x) * v.z;
            const double z = static_cast<double>(u.x) * v.y - static_cast<double>(u.y) * v.x;
            const double length = std::sqrt(x * x + y * y + z * z);
            return Vector3(static_cast<float>(x / length), static_cast<float>(y / length), static_cast<float>(z / length));
        }
    }

    std::optional<TerrainSurface> sampleTerrainSurface(const Heightmap& heights,
        const TerrainMeshSettings& settings, const float x, const float z) noexcept
    {
        if (!std::isfinite(x) || !std::isfinite(z))
        {
            LOG_ERROR("[TerrainSurface] Local query coordinates must be finite.");
            return std::nullopt;
        }
        if (x < -settings.sizeX * 0.5f || x > settings.sizeX * 0.5f
            || z < -settings.sizeZ * 0.5f || z > settings.sizeZ * 0.5f)
            return std::nullopt;
        const auto column = gridCell(x, heights.width(), settings.sizeX);
        const auto row = gridCell(z, heights.height(), settings.sizeZ);
        const Vector3 a = vertex(heights, settings, column, row);
        const Vector3 b = vertex(heights, settings, column + 1, row);
        const Vector3 c = vertex(heights, settings, column, row + 1);
        const Vector3 d = vertex(heights, settings, column + 1, row + 1);
        // Use the rounded mesh positions rather than an idealized grid when interpolating.
        const double u = std::clamp((static_cast<double>(x) - a.x) / (b.x - a.x), 0.0, 1.0);
        const double v = std::clamp((static_cast<double>(z) - a.z) / (c.z - a.z), 0.0, 1.0);
        const bool first = u + v <= 1;
        const double y = first ? a.y + u * (b.y - a.y) + v * (c.y - a.y)
            : d.y + (1 - u) * (c.y - d.y) + (1 - v) * (b.y - d.y);
        return TerrainSurface{ Vector3(x, static_cast<float>(y), z), first ? normal(a, c, b) : normal(b, c, d) };
    }

    std::optional<TerrainSurface> sampleTerrainSurfaceWorld(const Heightmap& heights,
        const TerrainMeshSettings& settings, const Matrix& world, const Vector3& position) noexcept
    {
        const auto matrixValues = std::span(&world._11, 16);
        const float determinant = world.Determinant();
        if (!finite(position) || !std::ranges::all_of(matrixValues, [](float value) { return std::isfinite(value); })
            || !std::isfinite(determinant) || determinant == 0 || world._14 != 0 || world._24 != 0
            || world._34 != 0 || world._44 != 1)
        {
            LOG_ERROR("[TerrainSurface] World queries require finite coordinates and an invertible affine transform.");
            return std::nullopt;
        }
        const Matrix inverse = world.Invert();
        if (!std::ranges::all_of(std::span(&inverse._11, 16), [](float value) { return std::isfinite(value); }))
        {
            LOG_ERROR("[TerrainSurface] Terrain transform cannot be inverted accurately.");
            return std::nullopt;
        }
        float bottom = std::numeric_limits<float>::max();
        float top = std::numeric_limits<float>::lowest();
        for (const float x : { -settings.sizeX * 0.5f, settings.sizeX * 0.5f })
            for (const float y : { settings.heightOffset, settings.heightOffset + settings.heightScale })
                for (const float z : { -settings.sizeZ * 0.5f, settings.sizeZ * 0.5f })
                {
                    const auto point = Vector3::Transform(Vector3(x, y, z), world);
                    bottom = std::min(bottom, point.y);
                    top = std::max(top, point.y);
                }
        const float margin = std::max(1.0f, std::max(std::abs(top), std::abs(bottom)) * 0.00001f);
        const auto origin = Vector3::Transform(Vector3(position.x, top + margin, position.z), inverse);
        const auto direction = Vector3::TransformNormal(-Vector3::UnitY, inverse);
        if (!finite(origin) || !finite(direction) || !std::isfinite(top - bottom + 2 * margin))
        {
            LOG_ERROR("[TerrainSurface] World query exceeds representable coordinates.");
            return std::nullopt;
        }
        double enter = 0;
        double leave = static_cast<double>(top) - bottom + 2 * margin;
        const auto clip = [&](const double start, const double delta, const double half)
        {
            if (delta == 0)
                return start >= -half && start <= half;
            double a = (-half - start) / delta;
            double b = (half - start) / delta;
            if (a > b)
                std::swap(a, b);
            enter = std::max(enter, a);
            leave = std::min(leave, b);
            return enter <= leave;
        };
        if (!clip(origin.x, direction.x, settings.sizeX * 0.5) || !clip(origin.z, direction.z, settings.sizeZ * 0.5))
            return std::nullopt;
        int column = static_cast<int>(gridCell(origin.x + enter * direction.x, heights.width(), settings.sizeX));
        int row = static_cast<int>(gridCell(origin.z + enter * direction.z, heights.height(), settings.sizeZ));
        const int stepX = direction.x < 0 ? -1 : 1;
        const int stepZ = direction.z < 0 ? -1 : 1;
        const auto nextBoundary = [&](const int cell, const int step, const std::uint32_t count,
            const float size, const double start, const double delta)
        {
            return delta == 0 ? std::numeric_limits<double>::infinity()
                : (gridPosition(static_cast<std::uint32_t>(cell + (step > 0 ? 1 : 0)), count, size) - start) / delta;
        };
        const auto intersect = [&](const Vector3& a, const Vector3& b, const Vector3& c) -> std::optional<double>
        {
            const Vector3 n = normal(a, b, c);
            const double denominator = static_cast<double>(n.x) * direction.x + static_cast<double>(n.y) * direction.y
                + static_cast<double>(n.z) * direction.z;
            if (std::abs(denominator) < 1e-12)
                return std::nullopt;
            const double t = (static_cast<double>(n.x) * (static_cast<double>(a.x) - origin.x)
                + static_cast<double>(n.y) * (static_cast<double>(a.y) - origin.y)
                + static_cast<double>(n.z) * (static_cast<double>(a.z) - origin.z)) / denominator;
            if (t < enter - 1e-5 || t > leave + 1e-5)
                return std::nullopt;
            const double x = origin.x + t * direction.x;
            const double z = origin.z + t * direction.z;
            const double abx = static_cast<double>(b.x) - a.x, abz = static_cast<double>(b.z) - a.z;
            const double acx = static_cast<double>(c.x) - a.x, acz = static_cast<double>(c.z) - a.z;
            const double area = abx * acz - abz * acx;
            const double u = ((x - a.x) * acz - (z - a.z) * acx) / area;
            const double v = (abx * (z - a.z) - abz * (x - a.x)) / area;
            return u >= -1e-6 && v >= -1e-6 && u + v <= 1.000001 ? std::optional(t) : std::nullopt;
        };
        for (std::uint32_t iteration = 0; iteration < heights.width() + heights.height(); ++iteration)
        {
            const auto x = static_cast<std::uint32_t>(column), z = static_cast<std::uint32_t>(row);
            const auto a = vertex(heights, settings, x, z), b = vertex(heights, settings, x + 1, z);
            const auto c = vertex(heights, settings, x, z + 1), d = vertex(heights, settings, x + 1, z + 1);
            const auto first = intersect(a, c, b), second = intersect(b, c, d);
            if (first || second)
            {
                const bool useFirst = first && (!second || *first <= *second);
                const double t = useFirst ? *first : *second;
                auto n = Vector3::TransformNormal(useFirst ? normal(a, c, b) : normal(b, c, d), inverse.Transpose());
                n.Normalize();
                if (n.y < 0)
                    n = -n;
                return TerrainSurface{ Vector3(position.x, static_cast<float>(top + margin - t), position.z), n };
            }
            const double tx = nextBoundary(column, stepX, heights.width(), settings.sizeX, origin.x, direction.x);
            const double tz = nextBoundary(row, stepZ, heights.height(), settings.sizeZ, origin.z, direction.z);
            const double next = std::min(tx, tz);
            if (!std::isfinite(next) || next > leave)
                break;
            if (tx <= tz)
                column += stepX;
            if (tz <= tx)
                row += stepZ;
            if (column < 0 || row < 0 || column >= static_cast<int>(heights.width()) - 1
                || row >= static_cast<int>(heights.height()) - 1)
                break;
        }
        return std::nullopt;
    }
}
