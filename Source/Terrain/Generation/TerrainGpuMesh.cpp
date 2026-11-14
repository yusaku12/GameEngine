#include "Pch.h"
#include "Terrain\Generation\TerrainGpuMesh.h"
#include "Terrain\Generation\TerrainBuild.h"
#include "Terrain\Data\Heightmap.h"
#include "Assets\Model\Resource\ModelResource.h"
#include "Graphics\Shader\Shader.h"
#include "Core\Logging\Logger.h"
#include "Core\Math\MathFunction.h"
#include <cmath>
#include <limits>
#include <numbers>

namespace Engine
{
    namespace
    {
        struct Sample { float height, y; };
        struct Axis { float position, uv; };
        struct Layer
        {
            float minimumHeight, maximumHeight, heightBlend;
            float minimumSlope, maximumSlope, slopeBlend, strength;
        };
        struct Recipe { std::uint32_t width, height, enabled, layerCount; };
        static_assert(sizeof(Sample) == 8 && sizeof(Axis) == 8 && sizeof(Layer) == 28 && sizeof(Recipe) == 16);
        static_assert(sizeof(ModelVertex) == 96 && offsetof(ModelVertex, position) == 0
            && offsetof(ModelVertex, normal) == 12 && offsetof(ModelVertex, tangent) == 24
            && offsetof(ModelVertex, bitangent) == 36 && offsetof(ModelVertex, texCoord) == 48
            && offsetof(ModelVertex, color) == 56 && offsetof(ModelVertex, boneIndices) == 72
            && offsetof(ModelVertex, boneWeights) == 80);

        bool needsCpuWeights(const TerrainMaterialSettings& material)
        {
            if (!material.enabled)
                return false;
            for (std::size_t index = 1; index < material.layerCount; ++index)
            {
                const auto& layer = material.layers[index];
                // Narrow slope bands amplify CPU/GPU normal and acos differences into visible classification changes.
                if ((layer.slopeBlend <= 1.0f && (layer.minimumSlope > 0.0f || layer.maximumSlope < 90.0f))
                    || std::fpclassify(layer.minimumHeight) == FP_SUBNORMAL
                    || std::fpclassify(layer.maximumHeight) == FP_SUBNORMAL
                    || std::fpclassify(layer.heightBlend) == FP_SUBNORMAL
                    || std::fpclassify(layer.minimumSlope) == FP_SUBNORMAL
                    || std::fpclassify(layer.maximumSlope) == FP_SUBNORMAL
                    || std::fpclassify(layer.slopeBlend) == FP_SUBNORMAL
                    || std::fpclassify(layer.strength) == FP_SUBNORMAL)
                    return true;
            }
            return false;
        }

        bool correctCpuWeights(const Heightmap& heights, const TerrainMaterialSettings& material, MeshResource& mesh,
            const std::atomic<bool>* cancelled)
        {
            std::vector<Vector3> normals(mesh.vertices.size(), Vector3::Zero);
            for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
            {
                if (index % 24576 == 0 && cancelled && cancelled->load(std::memory_order_acquire))
                    return false;
                const auto a = mesh.indices[index], b = mesh.indices[index + 1], c = mesh.indices[index + 2];
                const Vector3 normal = (mesh.vertices[b].position - mesh.vertices[a].position)
                    .Cross(mesh.vertices[c].position - mesh.vertices[a].position);
                normals[a] += normal;
                normals[b] += normal;
                normals[c] += normal;
            }
            for (std::size_t index = 0; index < normals.size(); ++index)
            {
                if (index % 4096 == 0 && cancelled && cancelled->load(std::memory_order_acquire))
                    return false;
                normals[index].Normalize();
                const float slope = std::acos(std::clamp(normals[index].y, 0.0f, 1.0f))
                    * 180.0f / std::numbers::pi_v<float>;
                mesh.vertices[index].color = terrainLayerWeights(material, heights.samples()[index], slope);
            }
            return true;
        }
    }

    TerrainGpuMesh& TerrainGpuMesh::instance()
    {
        static TerrainGpuMesh backend;
        return backend;
    }

    bool TerrainGpuMesh::initialize(ID3D12Device& device)
    {
        if (!shutdown())
            return false;
        const std::scoped_lock lock(m_mutex);
        m_device = &device;
        DX12Shader shader;
        std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants = { 0, 0, sizeof(Recipe) / sizeof(std::uint32_t) };
        for (std::uint32_t index = 0; index < 3; ++index)
        {
            parameters[index + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameters[index + 1].Descriptor.ShaderRegister = index;
        }
        parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        if (!shader.load({ "Assets\\Shaders\\Compiled\\TerrainMesh_csMain_cs.cso" })
            || !m_pipeline.initialize(device, shader, parameters)
            || !m_queue.initialize(device, DX12CommandQueueType::COMPUTE)
            || !m_fence.initialize(device) || !m_commands.initialize(device, DX12CommandQueueType::COMPUTE))
        {
            LOG_ERROR("[TerrainGpuMesh] Cannot initialize compute pipeline/queue.");
            return false;
        }
        m_available.store(true, std::memory_order_release);
        m_initialized.store(true, std::memory_order_release);
        return true;
    }

    bool TerrainGpuMesh::drain()
    {
        if (!m_submitted)
            return true;
        if (m_pendingFence == 0)
            m_pendingFence = m_fence.signal(*m_queue.get());
        if (m_pendingFence == 0 || !m_fence.waitOnCpu(m_pendingFence))
        {
            LOG_CRITICAL("[TerrainGpuMesh] Cannot retire submitted compute work; resources retained.");
            return false;
        }
        m_submitted = false;
        m_pendingFence = 0;
        return true;
    }

    bool TerrainGpuMesh::shutdown()
    {
        m_available.store(false, std::memory_order_release);
        const std::scoped_lock lock(m_mutex);
        if (!drain() || !m_readback.finalize() || !m_samples.finalize()
            || !m_axes.finalize() || !m_layers.finalize())
            return false;
        m_initialized.store(false, std::memory_order_release);
        m_output.finalize();
        m_commands.finalize();
        m_pipeline.finalize();
        m_fence.finalize();
        m_queue.finalize();
        m_device.Reset();
        return true;
    }

    bool TerrainGpuMesh::generate(const Heightmap& heights, const TerrainMeshSettings& settings,
        const TerrainMaterialSettings& material, ModelResource& destination,
        const std::atomic<bool>* cancelled, std::atomic<TerrainGenerationStage>* stage)
    {
        if (!validateTerrainMeshSettings(settings) || !validateTerrainMaterial(material))
            return false;
        const auto width = heights.width(), height = heights.height();
        if (width < 2 || height < 2 || width > Heightmap::MAX_DIMENSION || height > Heightmap::MAX_DIMENSION
            || heights.samples().size() != static_cast<std::size_t>(width) * height)
        {
            LOG_ERROR("[TerrainGpuMesh] Invalid heightmap dimensions or sample count.");
            return false;
        }
        const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (stopped())
            return false;
        const std::scoped_lock lock(m_mutex);
        if (!m_available.load(std::memory_order_acquire))
        {
            LOG_ERROR("[TerrainGpuMesh] Compute backend is unavailable.");
            return false;
        }
        if (!drain())
            return false;

        std::vector<Sample> samples;
        samples.reserve(heights.samples().size());
        float minimumY = std::numeric_limits<float>::max();
        float maximumY = std::numeric_limits<float>::lowest();
        bool correctWeights = needsCpuWeights(material);
        for (std::uint32_t z = 0; z < height; ++z)
        {
            if (stopped())
                return false;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const float sample = heights.samples()[static_cast<std::size_t>(z) * width + x];
                const float y = settings.heightOffset + sample * settings.heightScale;
                correctWeights = correctWeights || (material.enabled && std::fpclassify(sample) == FP_SUBNORMAL);
                samples.push_back({ sample, y });
                minimumY = std::min(minimumY, y);
                maximumY = std::max(maximumY, y);
            }
        }
        std::vector<Axis> axes;
        axes.reserve(static_cast<std::size_t>(width) + height);
        const auto appendAxis = [&](const std::uint32_t count, const float size, const float repeat)
        {
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const float u = static_cast<float>(index) / static_cast<float>(count - 1);
                axes.push_back({ (u - 0.5f) * size, u * repeat });
            }
        };
        appendAxis(width, settings.sizeX, settings.uvRepeatX);
        appendAxis(height, settings.sizeZ, settings.uvRepeatZ);
        std::array<Layer, TERRAIN_LAYER_COUNT> layers{};
        for (std::size_t index = 0; index < layers.size(); ++index)
        {
            const auto& layer = material.layers[index];
            layers[index] = { layer.minimumHeight, layer.maximumHeight, layer.heightBlend,
                layer.minimumSlope, layer.maximumSlope, layer.slopeBlend, layer.strength };
        }
        const auto vertexBytes = static_cast<std::uint64_t>(width) * height * sizeof(ModelVertex);
        const auto indexCount = static_cast<std::uint64_t>(width - 1) * (height - 1) * 6;
        const auto outputBytes = vertexBytes + indexCount * sizeof(std::uint32_t);
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = outputBytes;
        description.Height = 1;
        description.DepthOrArraySize = description.MipLevels = 1;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (!m_samples.initialize(*m_device.Get(), m_fence, samples.size() * sizeof(Sample))
            || !m_samples.write(std::as_bytes(std::span(samples)))
            || !m_axes.initialize(*m_device.Get(), m_fence, axes.size() * sizeof(Axis))
            || !m_axes.write(std::as_bytes(std::span(axes)))
            || !m_layers.initialize(*m_device.Get(), m_fence, sizeof(layers))
            || !m_layers.write(std::as_bytes(std::span(layers)))
            || !m_readback.initialize(*m_device.Get(), m_fence, outputBytes)
            || !m_output.initialize(*m_device.Get(), { .description = description })
            || !m_commands.begin(m_fence))
            return false;
        if (!m_pipeline.bind(m_commands) || !m_output.transition(m_commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        {
            if (!m_commands.close())
                m_available.store(false, std::memory_order_release);
            return false;
        }
        const Recipe recipe{ width, height, material.enabled ? 1u : 0u, material.layerCount };
        auto* list = m_commands.getForRecording();
        list->SetComputeRoot32BitConstants(0, sizeof(Recipe) / sizeof(std::uint32_t), &recipe, 0);
        list->SetComputeRootShaderResourceView(1, m_samples.getGpuVirtualAddress());
        list->SetComputeRootShaderResourceView(2, m_axes.getGpuVirtualAddress());
        list->SetComputeRootShaderResourceView(3, m_layers.getGpuVirtualAddress());
        list->SetComputeRootUnorderedAccessView(4, m_output.get()->GetGPUVirtualAddress());
        list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        if (!m_output.transition(m_commands, D3D12_RESOURCE_STATE_COPY_SOURCE))
        {
            if (!m_commands.close())
                m_available.store(false, std::memory_order_release);
            return false;
        }
        list->CopyBufferRegion(m_readback.get(), 0, m_output.get(), 0, outputBytes);
        if (!m_commands.close())
        {
            m_available.store(false, std::memory_order_release);
            return false;
        }
        if (stopped())
            return false;
        ID3D12CommandList* lists[]{ m_commands.getForExecution() };
        if (!m_queue.execute(lists))
            return false;
        m_submitted = true;
        if (stage)
            stage->store(TerrainGenerationStage::GpuReadback, std::memory_order_relaxed);
        m_pendingFence = m_fence.signal(*m_queue.get());
        if (m_pendingFence == 0 || !m_commands.markSubmitted(m_pendingFence)
            || !m_samples.markUsed(m_pendingFence) || !m_axes.markUsed(m_pendingFence)
            || !m_layers.markUsed(m_pendingFence) || !m_readback.markPending(m_pendingFence) || !drain())
        {
            m_available.store(false, std::memory_order_release);
            LOG_ERROR("[TerrainGpuMesh] Compute submission/readback synchronization failed.");
            return false;
        }
        if (stopped())
            return false;
        ModelResource model;
        MeshResource mesh;
        mesh.name = "Terrain";
        mesh.vertices.resize(static_cast<std::size_t>(width) * height);
        mesh.indices.resize(static_cast<std::size_t>(indexCount));
        if (!m_readback.read(std::as_writable_bytes(std::span(mesh.vertices)))
            || !m_readback.read(std::as_writable_bytes(std::span(mesh.indices)), vertexBytes) || stopped())
            return false;
        if ((correctWeights && !correctCpuWeights(heights, material, mesh, cancelled)) || stopped())
            return false;
        mesh.subMeshes.push_back({ .indexCount = static_cast<std::uint32_t>(indexCount) });
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
