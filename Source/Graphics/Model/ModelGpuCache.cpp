#include "Pch.h"
#include "Graphics\Model\ModelGpuCache.h"
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Fence.h"

namespace Engine
{
    ModelGpuCache::~ModelGpuCache()
    {
        finalize();
    }

    bool ModelGpuCache::initialize(DX12Device& device, DX12Fence& fence) noexcept
    {
        if (device.get() == nullptr)
            return false;
        if (!finalize())
            return false;
        m_device = &device;
        m_fence = &fence;
        return true;
    }

    bool ModelGpuCache::finalize()
    {
        bool succeeded = true;
        for (auto& [key, resource] : m_resources)
        {
            GE_UNUSED(key);
            succeeded = resource->bonePaletteBuffer.finalize() && succeeded;
            for (const std::unique_ptr<ModelGpuMesh>& mesh : resource->meshes)
            {
                if (mesh == nullptr)
                    continue;
                succeeded = mesh->indexBuffer.finalize() && succeeded;
                succeeded = mesh->vertexBuffer.finalize() && succeeded;
            }
        }
        m_resources.clear();
        m_device = nullptr;
        m_fence = nullptr;
        return succeeded;
    }

    ModelGpuResource* ModelGpuCache::getOrCreate(const ModelHandle handle)
    {
        if (m_device == nullptr || m_fence == nullptr || !handle.isValid())
            return nullptr;
        const std::uint64_t key = makeKey(handle);
        if (const auto found = m_resources.find(key); found != m_resources.end())
            return found->second.get();

        std::unique_ptr<ModelGpuResource> resource = createResource(handle);
        if (resource == nullptr)
            return nullptr;
        ModelGpuResource* const result = resource.get();
        m_resources.emplace(key, std::move(resource));
        return result;
    }

    bool ModelGpuCache::markUsed(const ModelHandle handle, const std::uint64_t fenceValue)
    {
        const auto found = m_resources.find(makeKey(handle));
        if (found == m_resources.end())
            return false;

        bool succeeded = true;
        succeeded = found->second->bonePaletteBuffer.markUsed(fenceValue) && succeeded;
        for (const std::unique_ptr<ModelGpuMesh>& mesh : found->second->meshes)
        {
            if (mesh == nullptr)
                continue;
            succeeded = mesh->vertexBuffer.markUsed(fenceValue) && succeeded;
            succeeded = mesh->indexBuffer.markUsed(fenceValue) && succeeded;
        }
        return succeeded;
    }

    std::uint64_t ModelGpuCache::makeKey(const ModelHandle handle) noexcept
    {
        return (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
    }

    std::unique_ptr<ModelGpuResource> ModelGpuCache::createResource(const ModelHandle handle)
    {
        std::shared_ptr<const ModelResource> source = ModelManager::instance().get(handle);
        if (source == nullptr)
            return nullptr;

        auto resource = std::make_unique<ModelGpuResource>();
        resource->source = std::move(source);
        std::vector<Matrix> nodeTransforms(resource->source->nodes.size(), Matrix::Identity);
        for (std::size_t nodeIndex = 0; nodeIndex < resource->source->nodes.size(); ++nodeIndex)
        {
            const ModelNode& node = resource->source->nodes[nodeIndex];
            nodeTransforms[nodeIndex] = node.localTransform;
            if (node.parentIndex >= 0 && static_cast<std::size_t>(node.parentIndex) < nodeIndex)
                nodeTransforms[nodeIndex] *= nodeTransforms[static_cast<std::size_t>(node.parentIndex)];
        }

        std::array<Matrix, MAX_SKINNING_BONES> bonePalette;
        bonePalette.fill(Matrix::Identity);
        if (!resource->bonePaletteBuffer.initialize(*m_device->get(), *m_fence, sizeof(bonePalette))
            || !resource->bonePaletteBuffer.write(std::as_bytes(std::span{ bonePalette })))
        {
            LOG_ERROR("[ModelGpuCache] Bone Palette Bufferの作成に失敗しました");
            return nullptr;
        }
        resource->meshes.reserve(resource->source->meshes.size());
        for (const MeshResource& sourceMesh : resource->source->meshes)
        {
            if (sourceMesh.vertices.empty() || sourceMesh.indices.empty())
            {
                LOG_WARNING("[ModelGpuCache] 空のMeshをスキップします: {}", sourceMesh.name);
                resource->meshes.push_back(nullptr);
                continue;
            }

            auto mesh = std::make_unique<ModelGpuMesh>();
            const std::span<const ModelVertex> vertices = sourceMesh.vertices;
            const std::span<const std::uint32_t> indices = sourceMesh.indices;
            if (!mesh->vertexBuffer.initialize(*m_device->get(), *m_fence, std::as_bytes(vertices).size())
                || !mesh->indexBuffer.initialize(*m_device->get(), *m_fence, std::as_bytes(indices).size())
                || !mesh->vertexBuffer.write(std::as_bytes(vertices))
                || !mesh->indexBuffer.write(std::as_bytes(indices)))
            {
                LOG_ERROR("[ModelGpuCache] MeshのGPU Uploadに失敗しました: {}", sourceMesh.name);
                return nullptr;
            }

            mesh->vertexBufferView = {
                .BufferLocation = mesh->vertexBuffer.getGpuVirtualAddress(),
                .SizeInBytes = static_cast<UINT>(mesh->vertexBuffer.getSize()),
                .StrideInBytes = sizeof(ModelVertex),
            };
            mesh->indexBufferView = {
                .BufferLocation = mesh->indexBuffer.getGpuVirtualAddress(),
                .SizeInBytes = static_cast<UINT>(mesh->indexBuffer.getSize()),
                .Format = DXGI_FORMAT_R32_UINT,
            };
            const std::size_t meshIndex = resource->meshes.size();
            for (std::size_t nodeIndex = 0; nodeIndex < resource->source->nodes.size(); ++nodeIndex)
            {
                const ModelNode& node = resource->source->nodes[nodeIndex];
                if (std::find(node.meshIndices.begin(), node.meshIndices.end(), meshIndex) != node.meshIndices.end())
                {
                    mesh->nodeTransform = nodeTransforms[nodeIndex];
                    break;
                }
            }
            resource->meshes.push_back(std::move(mesh));
        }
        return resource;
    }
} // namespace Engine