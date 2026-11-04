#include "Pch.h"
#include "Terrain\Vegetation\TerrainVegetation.h"
#include "Terrain\Data\Heightmap.h"
#include "Terrain\Geometry\TerrainMeshBuilder.h"
#include "Terrain\Geometry\TerrainSurface.h"
#include <cmath>
#include <numbers>
#include <random>
#include <unordered_map>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\MathFunction.h"

namespace Engine
{
    bool validateTerrainVegetation(const TerrainVegetationSettings& settings, const TerrainMeshSettings& mesh)
    {
        if (!validateTerrainMeshSettings(mesh))
            return false;
        const auto range = [](const float value, const float minimum, const float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        };
        for (const auto& layer : settings.layers)
        {
            auto extension = layer.modelPath.extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](const wchar_t value) { return std::towlower(value); });
            if (!range(layer.density, 0, 100) || !range(layer.minimumSpacing, 0.1f, 10000)
                || layer.maxInstances < 1 || layer.maxInstances > 25000
                || !range(layer.minimumHeight, 0, 1) || !range(layer.maximumHeight, layer.minimumHeight, 1)
                || !range(layer.minimumSlope, 0, 90) || !range(layer.maximumSlope, layer.minimumSlope, 90)
                || !range(layer.minimumScale, 0.001f, 1000) || !range(layer.maximumScale, layer.minimumScale, 1000)
                || !range(layer.heightOffset, -1000, 1000)
                || (settings.enabled && layer.enabled && extension != L".model" && extension != L".mdl"))
            {
                LOG_ERROR("[TerrainVegetation] Invalid layer bounds, density, spacing, count or static model asset path.");
                return false;
            }
            if (settings.enabled && layer.enabled
                && std::ceil(static_cast<double>(mesh.sizeX) * mesh.sizeZ * layer.density) > TERRAIN_VEGETATION_MAX_CANDIDATES)
            {
                LOG_ERROR("[TerrainVegetation] Candidate budget exceeds {} per layer; reduce density or terrain size.",
                    TERRAIN_VEGETATION_MAX_CANDIDATES);
                return false;
            }
        }
        return true;
    }

    bool buildTerrainVegetation(const Heightmap& heights, const TerrainMeshSettings& mesh,
        const TerrainVegetationSettings& settings, TerrainVegetationPlacement& destination, const std::atomic<bool>* cancelled)
    {
        if (!validateTerrainVegetation(settings, mesh))
            return false;
        if (heights.width() < 2 || heights.height() < 2 || heights.width() > Heightmap::MAX_DIMENSION
            || heights.height() > Heightmap::MAX_DIMENSION
            || heights.samples().size() != static_cast<std::size_t>(heights.width()) * heights.height())
        {
            LOG_ERROR("[TerrainVegetation] Invalid or moved-from heightmap.");
            return false;
        }
        const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (stopped())
            return false;
        TerrainVegetationPlacement prepared;
        auto normalizedMesh = mesh;
        normalizedMesh.heightScale = 1;
        normalizedMesh.heightOffset = 0;
        for (std::size_t index = 0; index < settings.layers.size(); ++index)
        {
            const auto& layer = settings.layers[index];
            if (!settings.enabled || !layer.enabled)
                continue;
            const double candidates = std::ceil(static_cast<double>(mesh.sizeX) * mesh.sizeZ * layer.density);
            auto transforms = std::make_shared<std::vector<Matrix>>();
            transforms->reserve(std::min(layer.maxInstances, static_cast<std::uint32_t>(candidates)));
            std::vector<Vector2> positions;
            positions.reserve(transforms->capacity());
            std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells;
            const auto key = [](const std::int32_t x, const std::int32_t z)
            {
                return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32)
                    | static_cast<std::uint32_t>(z);
            };
            const double spacingSquared = static_cast<double>(layer.minimumSpacing) * layer.minimumSpacing;
            std::mt19937 random(layer.seed);
            const auto unit = [&] { return static_cast<double>(random()) / 4294967296.0; };
            for (std::uint32_t candidate = 0; candidate < static_cast<std::uint32_t>(candidates); ++candidate)
            {
                if (stopped())
                    return false;
                const float x = static_cast<float>((unit() - 0.5) * mesh.sizeX);
                const float z = static_cast<float>((unit() - 0.5) * mesh.sizeZ);
                const float yaw = static_cast<float>(unit() * 2 * std::numbers::pi);
                const float scale = static_cast<float>(std::lerp(static_cast<double>(layer.minimumScale),
                    static_cast<double>(layer.maximumScale), unit()));
                const auto surface = sampleTerrainSurface(heights, mesh, x, z);
                if (!surface)
                {
                    LOG_ERROR("[TerrainVegetation] Candidate unexpectedly missed validated terrain bounds.");
                    return false;
                }
                const auto normalizedSurface = sampleTerrainSurface(heights, normalizedMesh, x, z);
                if (!normalizedSurface)
                {
                    LOG_ERROR("[TerrainVegetation] Cannot sample normalized candidate height.");
                    return false;
                }
                const float normalizedHeight = normalizedSurface->position.y;
                const float slope = static_cast<float>(std::acos(std::clamp(surface->normal.y, -1.0f, 1.0f)) * 180 / std::numbers::pi);
                if (normalizedHeight < layer.minimumHeight || normalizedHeight > layer.maximumHeight
                    || slope < layer.minimumSlope || slope > layer.maximumSlope)
                    continue;
                const auto cellX = static_cast<std::int32_t>(std::floor(static_cast<double>(x) / layer.minimumSpacing));
                const auto cellZ = static_cast<std::int32_t>(std::floor(static_cast<double>(z) / layer.minimumSpacing));
                bool occupied = false;
                for (int dz = -1; dz <= 1 && !occupied; ++dz)
                    for (int dx = -1; dx <= 1 && !occupied; ++dx)
                    {
                        const auto found = cells.find(key(cellX + dx, cellZ + dz));
                        if (found == cells.end())
                            continue;
                        for (const auto other : found->second)
                        {
                            const double deltaX = static_cast<double>(positions[other].x) - x;
                            const double deltaZ = static_cast<double>(positions[other].y) - z;
                            if (deltaX * deltaX + deltaZ * deltaZ < spacingSquared)
                            {
                                occupied = true;
                                break;
                            }
                        }
                    }
                if (occupied)
                    continue;
                const Vector3 up = layer.alignToNormal ? surface->normal : Vector3::UnitY;
                Vector3 right = Vector3::UnitZ.Cross(up);
                if (right.LengthSquared() < 1e-8f)
                    right = Vector3::UnitX.Cross(up);
                right.Normalize();
                const Vector3 forward = right.Cross(up);
                const Vector3 rotatedRight = right * std::cos(yaw) + forward * std::sin(yaw);
                const Vector3 rotatedForward = rotatedRight.Cross(up);
                const Vector3 position = surface->position + up * layer.heightOffset;
                const Matrix transform(
                    rotatedRight.x * scale, rotatedRight.y * scale, rotatedRight.z * scale, 0,
                    up.x * scale, up.y * scale, up.z * scale, 0,
                    rotatedForward.x * scale, rotatedForward.y * scale, rotatedForward.z * scale, 0,
                    position.x, position.y, position.z, 1);
                cells[key(cellX, cellZ)].push_back(positions.size());
                positions.emplace_back(x, z);
                transforms->push_back(transform);
                if (transforms->size() == layer.maxInstances)
                    break;
            }
            prepared[index] = std::move(transforms);
        }
        if (stopped())
            return false;
        destination = std::move(prepared);
        return true;
    }
}
