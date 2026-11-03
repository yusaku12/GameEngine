#include "Pch.h"
#include "Terrain\TerrainLod.h"
#include "Terrain\TerrainChunks.h"
#include "Assets\Model\Resource\ModelResource.h"
#include <cmath>

namespace Engine
{
    bool validateTerrainLodSettings(const TerrainLodSettings& settings)
    {
        if (settings.maxLevel < 1 || settings.maxLevel > 4 || !std::isfinite(settings.distance)
            || settings.distance < 0.1f || settings.distance > 1000000.0f)
        {
            LOG_ERROR("[TerrainLod] Expected levels 1..4 and a finite distance in 0.1..1000000.");
            return false;
        }
        return true;
    }

    namespace
    {
        std::vector<std::uint32_t> interiorAxis(const std::uint32_t cells, const std::uint32_t step)
        {
            std::vector<std::uint32_t> result;
            for (std::uint32_t value = 1; value < cells - 1; value += step)
                result.push_back(value);
            result.push_back(cells - 1);
            return result;
        }

        std::vector<std::uint32_t> simplifiedIndices(const TerrainChunk& chunk, const std::uint32_t step)
        {
            std::vector<std::uint32_t> indices;
            const auto width = chunk.cellsX;
            const auto height = chunk.cellsZ;
            const auto stride = width + 1;
            const auto xs = interiorAxis(width, step);
            const auto zs = interiorAxis(height, step);
            const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c)
            {
                const auto ax = static_cast<std::int64_t>(a % stride), az = static_cast<std::int64_t>(a / stride);
                const auto bx = static_cast<std::int64_t>(b % stride), bz = static_cast<std::int64_t>(b / stride);
                const auto cx = static_cast<std::int64_t>(c % stride), cz = static_cast<std::int64_t>(c / stride);
                if ((bz - az) * (cx - ax) - (bx - ax) * (cz - az) < 0)
                    std::swap(b, c);
                indices.insert(indices.end(), { a, b, c });
            };
            for (std::size_t z = 0; z + 1 < zs.size(); ++z)
                for (std::size_t x = 0; x + 1 < xs.size(); ++x)
                {
                    const auto a = zs[z] * stride + xs[x], b = zs[z] * stride + xs[x + 1];
                    const auto c = zs[z + 1] * stride + xs[x], d = zs[z + 1] * stride + xs[x + 1];
                    triangle(a, c, b);
                    triangle(b, c, d);
                }
            const auto strip = [&](const std::uint32_t length, const auto& inner, const auto& outerVertex,
                const auto& innerVertex)
            {
                std::uint32_t outer = 0;
                std::size_t inside = 0;
                while (outer < length || inside + 1 < inner.size())
                {
                    const bool advanceOuter = outer < length && (inside + 1 == inner.size()
                        || static_cast<std::uint64_t>(outer + 1) * (length - 2)
                            <= static_cast<std::uint64_t>(inner[inside + 1] - 1) * length);
                    if (advanceOuter)
                    {
                        triangle(outerVertex(outer), innerVertex(inner[inside]), outerVertex(outer + 1));
                        ++outer;
                    }
                    else
                    {
                        triangle(outerVertex(outer), innerVertex(inner[inside]), innerVertex(inner[inside + 1]));
                        ++inside;
                    }
                }
            };
            strip(width, xs, [](auto x) { return x; }, [stride](auto x) { return stride + x; });
            strip(width, xs, [stride, height](auto x) { return height * stride + x; },
                [stride, height](auto x) { return (height - 1) * stride + x; });
            strip(height, zs, [stride](auto z) { return z * stride; }, [stride](auto z) { return z * stride + 1; });
            strip(height, zs, [stride, width](auto z) { return z * stride + width; },
                [stride, width](auto z) { return z * stride + width - 1; });
            return indices;
        }
    }

    bool buildTerrainLods(ModelResource& model, const std::span<const TerrainChunk> chunks,
        const TerrainLodSettings& settings, TerrainLodSnapshot& destination)
    {
        if (!validateTerrainLodSettings(settings))
            return false;
        if (chunks.empty() || chunks.size() != model.meshes.size())
        {
            LOG_ERROR("[TerrainLod] Chunk metadata must match generated meshes.");
            return false;
        }
        for (std::size_t index = 0; index < chunks.size(); ++index)
        {
            const auto& chunk = chunks[index];
            const auto& mesh = model.meshes[index];
            if (chunk.meshIndex != index || chunk.cellsX == 0 || chunk.cellsZ == 0
                || chunk.cellsX > 1024 || chunk.cellsZ > 1024
                || mesh.vertices.size() != static_cast<std::size_t>(chunk.cellsX + 1) * (chunk.cellsZ + 1)
                || mesh.subMeshes.size() != 1 || mesh.subMeshes[0].indexStart != 0
                || mesh.subMeshes[0].indexCount != static_cast<std::size_t>(chunk.cellsX) * chunk.cellsZ * 6
                || mesh.indices.size() != mesh.subMeshes[0].indexCount)
            {
                LOG_ERROR("[TerrainLod] Expected full-resolution generated chunk meshes without existing LOD tails.");
                return false;
            }
        }
        TerrainLodSnapshot snapshot;
        snapshot.settings = settings;
        snapshot.chunks.resize(chunks.size());
        for (std::size_t index = 0; index < chunks.size(); ++index)
        {
            auto& mesh = model.meshes[index];
            auto& lod = snapshot.chunks[index];
            lod.levels[0] = { 0, mesh.subMeshes[0].indexCount };
            if (!settings.enabled || chunks[index].cellsX <= 2 || chunks[index].cellsZ <= 2)
                continue;
            for (std::uint32_t level = 1; level <= settings.maxLevel; ++level)
            {
                auto indices = simplifiedIndices(chunks[index], 1u << level);
                if (indices.size() >= lod.levels[lod.levelCount - 1].count)
                    break;
                lod.levels[lod.levelCount++] = { static_cast<std::uint32_t>(mesh.indices.size()),
                    static_cast<std::uint32_t>(indices.size()) };
                mesh.indices.insert(mesh.indices.end(), indices.begin(), indices.end());
            }
        }
        destination = std::move(snapshot);
        return true;
    }

    std::uint32_t selectTerrainLod(const TerrainLodSettings& settings, const AABB& bounds,
        const Vector3& camera, const std::uint32_t levelCount) noexcept
    {
        if (!settings.enabled || levelCount <= 1)
            return 0;
        const auto offset = camera - Vector3(bounds.Center);
        const auto delta = Vector3::Max(Vector3(std::abs(offset.x), std::abs(offset.y), std::abs(offset.z))
            - Vector3(bounds.Extents), Vector3::Zero);
        const double squared = static_cast<double>(delta.x) * delta.x + static_cast<double>(delta.y) * delta.y
            + static_cast<double>(delta.z) * delta.z;
        std::uint32_t level = 0;
        double threshold = settings.distance;
        while (level + 1 < levelCount && level < settings.maxLevel && squared >= threshold * threshold)
        {
            ++level;
            threshold *= 2;
        }
        return level;
    }
}
