#include "Pch.h"
#include "Assets\Model\Resource\ModelResource.h"
#include "Physics\PhysicsMeshAsset.h"

#include <cooking/PxCooking.h>
#include <extensions/PxDefaultAllocator.h>
#include <extensions/PxDefaultStreams.h>
#include <fstream>

namespace Engine
{
    namespace
    {
        constexpr std::uint32_t PHYSICS_ASSET_MAGIC = 0x53594850u;
        constexpr std::uint32_t PHYSICS_ASSET_VERSION = 1;
        constexpr std::uint64_t MAX_COOKED_DATA_SIZE = 1024ull * 1024ull * 1024ull;

    }

    std::shared_ptr<const PhysicsMeshAsset> PhysicsMeshAsset::cookTriangle(
        const std::span<const Vector3> vertices,
        const std::span<const std::uint32_t> indices)
    {
        if (vertices.size() < 3 || indices.size() < 3 || indices.size() % 3 != 0
            || vertices.size() > (std::numeric_limits<physx::PxU32>::max)()
            || indices.size() / 3 > (std::numeric_limits<physx::PxU32>::max)())
        {
            LOG_ERROR("[PhysicsMeshAsset] Triangle cooking requires at least one valid indexed triangle.");
            return nullptr;
        }

        std::vector<physx::PxVec3> points;
        try
        {
            points.reserve(vertices.size());
            for (const Vector3& vertex : vertices)
            {
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
                {
                    LOG_ERROR("[PhysicsMeshAsset] Mesh vertices must be finite.");
                    return nullptr;
                }
                points.emplace_back(vertex.x, vertex.y, vertex.z);
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to allocate triangle cooking vertices.");
            return nullptr;
        }

        for (const std::uint32_t index : indices)
        {
            if (index >= points.size())
            {
                LOG_ERROR("[PhysicsMeshAsset] Triangle index is outside the vertex array.");
                return nullptr;
            }
        }

        physx::PxTriangleMeshDesc description;
        description.points.count = static_cast<physx::PxU32>(points.size());
        description.points.stride = sizeof(physx::PxVec3);
        description.points.data = points.data();
        description.triangles.count = static_cast<physx::PxU32>(indices.size() / 3);
        description.triangles.stride = sizeof(std::uint32_t) * 3;
        description.triangles.data = indices.data();
        if (!description.isValid())
        {
            LOG_ERROR("[PhysicsMeshAsset] Triangle mesh descriptor is invalid.");
            return nullptr;
        }

        const physx::PxCookingParams params{ physx::PxTolerancesScale() };
        physx::PxDefaultAllocator allocator;
        physx::PxDefaultMemoryOutputStream output(allocator);
        if (!PxCookTriangleMesh(params, description, output)
            || output.getSize() == 0 || output.getSize() > MAX_COOKED_DATA_SIZE)
        {
            LOG_ERROR("[PhysicsMeshAsset] PhysX failed to cook a triangle mesh.");
            return nullptr;
        }
        try
        {
            std::vector<std::uint8_t> cookedData(
                output.getData(), output.getData() + output.getSize());
            return std::shared_ptr<const PhysicsMeshAsset>(
                new PhysicsMeshAsset(PhysicsMeshType::Triangle, std::move(cookedData)));
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to allocate cooked triangle mesh data.");
            return nullptr;
        }
    }

    std::shared_ptr<const PhysicsMeshAsset> PhysicsMeshAsset::cookTriangle(const MeshResource& mesh)
    {
        std::vector<Vector3> vertices;
        try
        {
            vertices.reserve(mesh.vertices.size());
            for (const ModelVertex& vertex : mesh.vertices)
                vertices.push_back(vertex.position);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to allocate model mesh vertices for cooking.");
            return nullptr;
        }
        return cookTriangle(vertices, mesh.indices);
    }

    std::shared_ptr<const PhysicsMeshAsset> PhysicsMeshAsset::cookConvex(
        const std::span<const Vector3> vertices)
    {
        if (vertices.size() < 4 || vertices.size() > 65535)
        {
            LOG_ERROR("[PhysicsMeshAsset] Convex cooking requires between 4 and 65535 vertices.");
            return nullptr;
        }

        std::vector<physx::PxVec3> points;
        try
        {
            points.reserve(vertices.size());
            for (const Vector3& vertex : vertices)
            {
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
                {
                    LOG_ERROR("[PhysicsMeshAsset] Convex vertices must be finite.");
                    return nullptr;
                }
                points.emplace_back(vertex.x, vertex.y, vertex.z);
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to allocate convex cooking vertices.");
            return nullptr;
        }

        physx::PxConvexMeshDesc description;
        description.points.count = static_cast<physx::PxU32>(points.size());
        description.points.stride = sizeof(physx::PxVec3);
        description.points.data = points.data();
        description.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;
        if (!description.isValid())
        {
            LOG_ERROR("[PhysicsMeshAsset] Convex mesh descriptor is invalid.");
            return nullptr;
        }

        const physx::PxCookingParams params{ physx::PxTolerancesScale() };
        physx::PxDefaultAllocator allocator;
        physx::PxDefaultMemoryOutputStream output(allocator);
        if (!PxCookConvexMesh(params, description, output)
            || output.getSize() == 0 || output.getSize() > MAX_COOKED_DATA_SIZE)
        {
            LOG_ERROR("[PhysicsMeshAsset] PhysX failed to cook a convex mesh.");
            return nullptr;
        }
        try
        {
            std::vector<std::uint8_t> cookedData(
                output.getData(), output.getData() + output.getSize());
            return std::shared_ptr<const PhysicsMeshAsset>(
                new PhysicsMeshAsset(PhysicsMeshType::Convex, std::move(cookedData)));
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to allocate cooked convex mesh data.");
            return nullptr;
        }
    }

    std::shared_ptr<const PhysicsMeshAsset> PhysicsMeshAsset::cookConvex(const MeshResource& mesh)
    {
        std::vector<Vector3> vertices;
        try
        {
            vertices.reserve(mesh.vertices.size());
            for (const ModelVertex& vertex : mesh.vertices)
                vertices.push_back(vertex.position);
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to allocate model mesh vertices for convex cooking.");
            return nullptr;
        }
        return cookConvex(vertices);
    }

    std::shared_ptr<const PhysicsMeshAsset> PhysicsMeshAsset::load(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint8_t typeValue = 0;
        std::uint64_t dataSize = 0;
        if (!stream
            || !stream.read(reinterpret_cast<char*>(&magic), sizeof(magic))
            || !stream.read(reinterpret_cast<char*>(&version), sizeof(version))
            || !stream.read(reinterpret_cast<char*>(&typeValue), sizeof(typeValue))
            || !stream.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize))
            || magic != PHYSICS_ASSET_MAGIC
            || version != PHYSICS_ASSET_VERSION
            || (typeValue != static_cast<std::uint8_t>(PhysicsMeshType::Triangle)
                && typeValue != static_cast<std::uint8_t>(PhysicsMeshType::Convex))
            || dataSize == 0 || dataSize > MAX_COOKED_DATA_SIZE)
        {
            LOG_ERROR("[PhysicsMeshAsset] Physics asset header is invalid: {}", path.string());
            return nullptr;
        }

        try
        {
            std::vector<std::uint8_t> cookedData(static_cast<std::size_t>(dataSize));
            if (!stream.read(reinterpret_cast<char*>(cookedData.data()),
                static_cast<std::streamsize>(cookedData.size())))
            {
                LOG_ERROR("[PhysicsMeshAsset] Physics asset data is truncated: {}", path.string());
                return nullptr;
            }
            if (stream.peek() != std::char_traits<char>::eof())
            {
                LOG_ERROR("[PhysicsMeshAsset] Physics asset contains trailing data: {}", path.string());
                return nullptr;
            }
            auto asset = std::shared_ptr<PhysicsMeshAsset>(
                new PhysicsMeshAsset(static_cast<PhysicsMeshType>(typeValue), std::move(cookedData)));
            asset->m_path = path.lexically_normal();
            return asset;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to allocate physics asset data: {}", path.string());
            return nullptr;
        }
    }

    bool PhysicsMeshAsset::save(const std::filesystem::path& path) const
    {
        if (path.empty() || m_cookedData.empty() || m_cookedData.size() > MAX_COOKED_DATA_SIZE)
        {
            LOG_ERROR("[PhysicsMeshAsset] Cannot save an empty or invalid physics asset path/data.");
            return false;
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        const std::uint32_t magic = PHYSICS_ASSET_MAGIC;
        const std::uint32_t version = PHYSICS_ASSET_VERSION;
        const std::uint8_t typeValue = static_cast<std::uint8_t>(m_type);
        const std::uint64_t dataSize = m_cookedData.size();
        if (!stream
            || !stream.write(reinterpret_cast<const char*>(&magic), sizeof(magic))
            || !stream.write(reinterpret_cast<const char*>(&version), sizeof(version))
            || !stream.write(reinterpret_cast<const char*>(&typeValue), sizeof(typeValue))
            || !stream.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize))
            || !stream.write(
                reinterpret_cast<const char*>(m_cookedData.data()),
                static_cast<std::streamsize>(m_cookedData.size())))
        {
            LOG_ERROR("[PhysicsMeshAsset] Failed to write physics asset: {}", path.string());
            return false;
        }
        m_path = path.lexically_normal();
        return true;
    }
} // namespace Engine
