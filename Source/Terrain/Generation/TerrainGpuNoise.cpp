#include "Pch.h"
#include "Terrain\Generation\TerrainGpuNoise.h"
#include "Terrain\Data\Heightmap.h"
#include "Terrain\Generation\TerrainBuild.h"
#include "Graphics\Shader\Shader.h"
#include <cmath>
#include "Core\Logging\Logger.h"

namespace Engine
{
    namespace
    {
        struct Coordinate
        {
            std::uint32_t low, high;
            float fraction;
            std::uint32_t padding = 0;
        };
        static_assert(sizeof(Coordinate) == 16);
        struct Recipe
        {
            std::uint32_t width, height, octaves, seed;
            float persistence;
        };
        static_assert(sizeof(Recipe) == 20);
    }

    TerrainGpuNoise& TerrainGpuNoise::instance()
    {
        static TerrainGpuNoise backend;
        return backend;
    }

    bool TerrainGpuNoise::initialize(ID3D12Device& device)
    {
        if (!shutdown())
            return false;
        const std::scoped_lock lock(m_mutex);
        m_device = &device;
        DX12Shader shader;
        std::array<D3D12_ROOT_PARAMETER, 3> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[0].Constants = { 0, 0, sizeof(Recipe) / sizeof(std::uint32_t) };
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].Descriptor.ShaderRegister = 0;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[2].Descriptor.ShaderRegister = 0;
        if (!shader.load({ "Assets\\Shaders\\Compiled\\TerrainNoise_csMain_cs.cso" })
            || !m_pipeline.initialize(device, shader, parameters)
            || !m_queue.initialize(device, DX12CommandQueueType::COMPUTE)
            || !m_fence.initialize(device) || !m_commands.initialize(device, DX12CommandQueueType::COMPUTE))
        {
            LOG_ERROR("[TerrainGpuNoise] Cannot initialize compute pipeline/queue.");
            return false;
        }
        m_available.store(true, std::memory_order_release);
        return true;
    }

    bool TerrainGpuNoise::drain()
    {
        if (!m_submitted)
            return true;
        if (m_pendingFence == 0)
            m_pendingFence = m_fence.signal(*m_queue.get());
        if (m_pendingFence == 0 || !m_fence.waitOnCpu(m_pendingFence))
        {
            LOG_CRITICAL("[TerrainGpuNoise] Cannot retire submitted compute work; resources retained.");
            return false;
        }
        m_submitted = false;
        m_pendingFence = 0;
        return true;
    }

    bool TerrainGpuNoise::shutdown()
    {
        m_available.store(false, std::memory_order_release);
        const std::scoped_lock lock(m_mutex);
        if (!drain() || !m_readback.finalize() || !m_coordinates.finalize())
            return false;
        m_output.finalize();
        m_commands.finalize();
        m_pipeline.finalize();
        m_fence.finalize();
        m_queue.finalize();
        m_device.Reset();
        return true;
    }

    bool TerrainGpuNoise::generate(const TerrainNoiseSettings& settings, Heightmap& destination, const std::atomic<bool>* cancelled,
        std::atomic<TerrainGenerationStage>* stage)
    {
        if (!validateTerrainNoiseSettings(settings))
            return false;
        const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (stopped())
            return false;
        const std::scoped_lock lock(m_mutex);
        if (!isAvailable())
        {
            LOG_ERROR("[TerrainGpuNoise] GPU backend is unavailable; initialize the renderer before requesting GPU noise.");
            return false;
        }
        if (!drain())
            return false;
        const std::uint32_t octaves = settings.mode == TerrainNoiseMode::Perlin ? 1 : settings.octaves;
        std::vector<Coordinate> coordinates;
        coordinates.reserve(static_cast<std::size_t>(settings.width + settings.height) * octaves);
        const auto appendAxis = [&](const std::uint32_t count, const float offset)
        {
            const std::size_t start = coordinates.size();
            coordinates.resize(start + static_cast<std::size_t>(count) * octaves);
            for (std::uint32_t sample = 0; sample < count; ++sample)
            {
                double value = static_cast<double>(sample) / (count - 1) * settings.frequency + offset;
                for (std::uint32_t octave = 0; octave < octaves; ++octave)
                {
                    const double cell = std::floor(value);
                    const auto bits = static_cast<std::uint64_t>(static_cast<std::int64_t>(cell));
                    coordinates[start + static_cast<std::size_t>(octave) * count + sample] = {
                        static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32),
                        static_cast<float>(value - cell) };
                    value *= settings.lacunarity;
                }
            }
        };
        appendAxis(settings.width, settings.offsetX);
        appendAxis(settings.height, settings.offsetZ);
        const auto bytes = static_cast<std::uint64_t>(settings.width) * settings.height * sizeof(float);
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = bytes;
        description.Height = 1;
        description.DepthOrArraySize = description.MipLevels = 1;
        description.SampleDesc = { 1, 0 };
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        std::vector<float> samples(static_cast<std::size_t>(settings.width) * settings.height);
        if (!m_coordinates.initialize(*m_device.Get(), m_fence, coordinates.size() * sizeof(Coordinate))
            || !m_coordinates.write(std::as_bytes(std::span(coordinates)))
            || !m_readback.initialize(*m_device.Get(), m_fence, bytes)
            || !m_output.initialize(*m_device.Get(), { .description = description })
            || !m_commands.begin(m_fence) || !m_pipeline.bind(m_commands)
            || !m_output.transition(m_commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            return false;
        const Recipe recipe{ settings.width, settings.height, octaves, settings.seed, settings.persistence };
        auto* list = m_commands.getForRecording();
        list->SetComputeRoot32BitConstants(0, sizeof(Recipe) / sizeof(std::uint32_t), &recipe, 0);
        list->SetComputeRootShaderResourceView(1, m_coordinates.getGpuVirtualAddress());
        list->SetComputeRootUnorderedAccessView(2, m_output.get()->GetGPUVirtualAddress());
        list->Dispatch((settings.width + 7) / 8, (settings.height + 7) / 8, 1);
        if (!m_output.transition(m_commands, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return false;
        list->CopyBufferRegion(m_readback.get(), 0, m_output.get(), 0, bytes);
        if (!m_commands.close())
            return false;
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
            || !m_coordinates.markUsed(m_pendingFence) || !m_readback.markPending(m_pendingFence)
            || !drain())
        {
            m_available.store(false, std::memory_order_release);
            LOG_ERROR("[TerrainGpuNoise] Compute submission/readback synchronization failed.");
            return false;
        }
        if (stopped() || !m_readback.read(std::as_writable_bytes(std::span(samples))) || stopped())
            return false;
        return destination.assign(settings.width, settings.height, samples);
    }
}
