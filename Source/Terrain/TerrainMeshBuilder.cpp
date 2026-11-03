#include "Pch.h"
#include "Terrain\TerrainMeshBuilder.h"
#include "Assets\Model\Resource\ModelResource.h"
#include "Terrain\Heightmap.h"
#include <cmath>
#include <limits>

namespace Engine
{
    bool validateTerrainMeshSettings(const TerrainMeshSettings& settings)
    {
        const auto inRange = [](const float value, const float minimum, const float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        };
        if (!inRange(settings.sizeX, 0.01f, 100000.0f) || !inRange(settings.sizeZ, 0.01f, 100000.0f)
            || !inRange(settings.heightScale, 0.0f, 100000.0f)
            || !inRange(settings.heightOffset, -100000.0f, 100000.0f)
            || !inRange(settings.uvRepeatX, 0.001f, 10000.0f)
            || !inRange(settings.uvRepeatZ, 0.001f, 10000.0f))
        {
            LOG_ERROR("[Terrain] Invalid mesh dimensions, height range or UV repeats.");
            return false;
        }
        return true;
    }

    bool buildTerrainModel(const Heightmap& heightmap, const TerrainMeshSettings& settings,
        ModelResource& destination)
    {
        if (!validateTerrainMeshSettings(settings))
            return false;
        if (heightmap.samples().size() != static_cast<std::size_t>(heightmap.width()) * heightmap.height())
        {
            LOG_ERROR("[Terrain] Heightmap sample count does not match its dimensions.");
            return false;
        }

        ModelResource model;
        MeshResource mesh;
        mesh.name = "Terrain";
        const std::uint32_t width = heightmap.width();
        const std::uint32_t height = heightmap.height();
        mesh.vertices.resize(static_cast<std::size_t>(width) * height);
        mesh.indices.reserve(static_cast<std::size_t>(width - 1) * (height - 1) * 6);
        float minimumY = std::numeric_limits<float>::max();
        float maximumY = std::numeric_limits<float>::lowest();
        for (std::uint32_t z = 0; z < height; ++z)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(z) * width + x;
                ModelVertex& vertex = mesh.vertices[index];
                const float u = static_cast<float>(x) / static_cast<float>(width - 1);
                const float v = static_cast<float>(z) / static_cast<float>(height - 1);
                const float y = settings.heightOffset + heightmap.samples()[index] * settings.heightScale;
                vertex.position = Vector3((u - 0.5f) * settings.sizeX, y, (v - 0.5f) * settings.sizeZ);
                vertex.texCoord = Vector2(u * settings.uvRepeatX, v * settings.uvRepeatZ);
                vertex.normal = Vector3::Zero;
                minimumY = std::min(minimumY, y);
                maximumY = std::max(maximumY, y);
            }
        }
        for (std::uint32_t z = 0; z + 1 < height; ++z)
        {
            for (std::uint32_t x = 0; x + 1 < width; ++x)
            {
                const std::uint32_t a = z * width + x;
                const std::uint32_t b = a + 1;
                const std::uint32_t c = a + width;
                const std::uint32_t d = c + 1;
                mesh.indices.insert(mesh.indices.end(), { a, c, b, b, c, d });
            }
        }
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
        {
            ModelVertex& a = mesh.vertices[mesh.indices[index]];
            ModelVertex& b = mesh.vertices[mesh.indices[index + 1]];
            ModelVertex& c = mesh.vertices[mesh.indices[index + 2]];
            const Vector3 normal = (b.position - a.position).Cross(c.position - a.position);
            a.normal += normal;
            b.normal += normal;
            c.normal += normal;
        }
        for (std::uint32_t z = 0; z < height; ++z)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                ModelVertex& vertex = mesh.vertices[static_cast<std::size_t>(z) * width + x];
                vertex.normal.Normalize();
                // This avoids cancellation when projecting a very steep X slope onto the normal plane.
                vertex.tangent = Vector3(vertex.normal.y, -vertex.normal.x, 0.0f);
                vertex.tangent.Normalize();
                vertex.bitangent = vertex.tangent.Cross(vertex.normal);
                vertex.bitangent.Normalize();
            }
        }
        mesh.subMeshes.push_back({ .indexCount = static_cast<std::uint32_t>(mesh.indices.size()) });
        const Vector3 minimum(-settings.sizeX * 0.5f, minimumY, -settings.sizeZ * 0.5f);
        const Vector3 maximum(settings.sizeX * 0.5f, maximumY, settings.sizeZ * 0.5f);
        mesh.boundingBox = makeAABB(minimum, maximum);
        mesh.boundingBox.Extents = Vector3::Max(maximum - Vector3(mesh.boundingBox.Center),
            Vector3(mesh.boundingBox.Center) - minimum);
        mesh.boundingSphere = BoundingSphere(mesh.boundingBox.Center, Vector3(mesh.boundingBox.Extents).Length());
        model.boundingBox = mesh.boundingBox;
        model.boundingSphere = mesh.boundingSphere;
        model.meshes.push_back(std::move(mesh));
        ModelNode node;
        node.name = "Terrain";
        node.meshIndices.push_back(0);
        model.nodes.push_back(std::move(node));
        destination = std::move(model);
        return true;
    }
}
