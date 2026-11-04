#include "Pch.h"
#include "Terrain\Geometry\TerrainChunks.h"
#include "Terrain\Data\Heightmap.h"
#include "Assets\Model\Resource\ModelResource.h"

namespace Engine
{
    bool validateTerrainChunkSettings(const TerrainChunkSettings& settings)
    {
        if (settings.cellsPerChunk < 16 || settings.cellsPerChunk > 1024)
        {
            LOG_ERROR("[TerrainChunks] Chunk edge must contain 16..1024 cells.");
            return false;
        }
        return true;
    }

    bool partitionTerrainModel(ModelResource& model, const std::uint32_t width, const std::uint32_t height,
        const TerrainChunkSettings& settings, std::vector<TerrainChunk>& chunks)
    {
        if (!validateTerrainChunkSettings(settings))
            return false;
        if (width < 2 || height < 2 || width > Heightmap::MAX_DIMENSION || height > Heightmap::MAX_DIMENSION
            || model.meshes.size() != 1 || model.meshes[0].vertices.size() != static_cast<std::size_t>(width) * height
            || model.meshes[0].indices.size() != static_cast<std::size_t>(width - 1) * (height - 1) * 6)
        {
            LOG_ERROR("[TerrainChunks] Expected one complete generated terrain grid.");
            return false;
        }
        const auto columns = (width - 2) / settings.cellsPerChunk + 1;
        const auto rows = (height - 2) / settings.cellsPerChunk + 1;
        std::vector<TerrainChunk> metadata;
        metadata.reserve(static_cast<std::size_t>(columns) * rows);
        if (columns == 1 && rows == 1)
        {
            metadata.push_back({ 0, 0, 0, 0, width - 1, height - 1, 0, model.boundingBox });
            chunks = std::move(metadata);
            return true;
        }
        ModelResource replacement;
        replacement.boundingBox = model.boundingBox;
        replacement.boundingSphere = model.boundingSphere;
        replacement.meshes.reserve(metadata.capacity());
        ModelNode node;
        node.name = "Terrain";
        node.meshIndices.reserve(metadata.capacity());
        const auto& source = model.meshes[0];
        for (std::uint32_t row = 0; row < rows; ++row)
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                TerrainChunk chunk{ column, row, column * settings.cellsPerChunk, row * settings.cellsPerChunk };
                chunk.cellsX = std::min(settings.cellsPerChunk, width - 1 - chunk.sampleX);
                chunk.cellsZ = std::min(settings.cellsPerChunk, height - 1 - chunk.sampleZ);
                chunk.meshIndex = static_cast<std::uint32_t>(replacement.meshes.size());
                MeshResource mesh;
                mesh.name = "Terrain " + std::to_string(column) + "," + std::to_string(row);
                const auto stride = chunk.cellsX + 1;
                mesh.vertices.reserve(static_cast<std::size_t>(stride) * (chunk.cellsZ + 1));
                mesh.indices.reserve(static_cast<std::size_t>(chunk.cellsX) * chunk.cellsZ * 6);
                Vector3 minimum = source.vertices[static_cast<std::size_t>(chunk.sampleZ) * width + chunk.sampleX].position;
                Vector3 maximum = minimum;
                for (std::uint32_t z = 0; z <= chunk.cellsZ; ++z)
                    for (std::uint32_t x = 0; x <= chunk.cellsX; ++x)
                    {
                        const auto& vertex = source.vertices[static_cast<std::size_t>(chunk.sampleZ + z) * width + chunk.sampleX + x];
                        mesh.vertices.push_back(vertex);
                        minimum = Vector3::Min(minimum, vertex.position);
                        maximum = Vector3::Max(maximum, vertex.position);
                    }
                for (std::uint32_t z = 0; z < chunk.cellsZ; ++z)
                    for (std::uint32_t x = 0; x < chunk.cellsX; ++x)
                    {
                        const auto a = z * stride + x;
                        const auto b = a + 1;
                        const auto c = a + stride;
                        const auto d = c + 1;
                        mesh.indices.insert(mesh.indices.end(), { a, c, b, b, c, d });
                    }
                mesh.subMeshes.push_back({ .indexCount = static_cast<std::uint32_t>(mesh.indices.size()) });
                mesh.boundingBox = makeAABB(minimum, maximum);
                // Center/half-range rounding can otherwise exclude an endpoint by one ULP.
                mesh.boundingBox.Extents = Vector3::Max(maximum - Vector3(mesh.boundingBox.Center),
                    Vector3(mesh.boundingBox.Center) - minimum);
                mesh.boundingSphere = BoundingSphere(mesh.boundingBox.Center, Vector3(mesh.boundingBox.Extents).Length());
                chunk.bounds = mesh.boundingBox;
                metadata.push_back(chunk);
                node.meshIndices.push_back(chunk.meshIndex);
                replacement.meshes.push_back(std::move(mesh));
            }
        replacement.nodes.push_back(std::move(node));
        model = std::move(replacement);
        chunks = std::move(metadata);
        return true;
    }
}
