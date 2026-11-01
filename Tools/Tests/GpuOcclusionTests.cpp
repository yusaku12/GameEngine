#include "Pch.h"
#include <d3d12sdklayers.h>
#include "Graphics\DirectX12\GpuOcclusionCulling.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\Shader\Shader.h"
#include "Graphics\Renderer\RenderQueue.h"

namespace
{
    using namespace Engine;
    using Microsoft::WRL::ComPtr;

    void require(const bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Instance
    {
        Matrix worldViewProjection;
        Matrix world;
    };
    static_assert(sizeof(Instance) == 128);

    struct TestObject
    {
        Vector3 center = Vector3(0, 0, 0.8f);
        Vector3 extents = Vector3(0.15f, 0.15f, 0.05f);
        bool skinned = false;
        RenderPassType pass = RenderPassType::Opaque;
        std::uint32_t indexStart = 3;
        std::int32_t baseVertex = 0;
    };

    class Tests
    {
    public:
        Tests(const std::filesystem::path& shaderDirectory, const bool warp)
        {
            ComPtr<ID3D12Debug> debug;
            require(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))), "D3D12 debug layer is required.");
            debug->EnableDebugLayer();
            ComPtr<ID3D12Debug1> validation;
            require(SUCCEEDED(debug.As(&validation)), "GPU validation interface is required.");
            validation->SetEnableGPUBasedValidation(TRUE);
            ComPtr<IDXGIFactory4> factory;
            require(SUCCEEDED(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory))), "Create factory.");
            ComPtr<IDXGIAdapter1> adapter;
            if (warp)
                require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))), "Create WARP adapter.");
            else
                require(SUCCEEDED(factory->EnumAdapters1(0, &adapter)), "Create hardware adapter.");
            require(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))), "Create device.");
            require(SUCCEEDED(m_device.As(&m_messages)), "Create debug info queue.");
            D3D12_COMMAND_QUEUE_DESC queueDescription{};
            queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            require(SUCCEEDED(m_device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&m_queue))), "Create queue.");
            require(m_fence.initialize(*m_device.Get()), "Create fence.");
            require(m_list.initialize(*m_device.Get(), DX12CommandQueueType::DIRECT), "Create command list.");
            require(m_cullingList.initialize(*m_device.Get(), DX12CommandQueueType::DIRECT)
                && m_colorList.initialize(*m_device.Get(), DX12CommandQueueType::DIRECT), "Create ordered pass lists.");
            DX12Shader hiz, cull, vertex, pixel;
            require(hiz.load({ "Assets\\Shaders\\Compiled\\HiZ_csMain_cs.cso" }), "Load Hi-Z shader.");
            require(cull.load({ "Assets\\Shaders\\Compiled\\Occlusion_csMain_cs.cso" }), "Load culling shader.");
            require(DX12GpuOcclusionCulling::createPipelines(*m_device.Get(), hiz, cull, m_hiz, m_cull), "Create culling pipelines.");
            require(vertex.load({ shaderDirectory / "vs.cso" }) && pixel.load({ shaderDirectory / "ps.cso" }), "Load test shaders.");
            D3D12_ROOT_PARAMETER root{};
            root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            root.Descriptor.ShaderRegister = 5;
            root.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            require(m_graphics.initialize(*m_device.Get(), {
                .vertexShader = &vertex, .pixelShader = &pixel,
                .rootParameters = std::span(&root, 1),
                .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
                .depthComparison = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                .enableDepthWrite = false, .cullMode = D3D12_CULL_MODE_NONE }), "Create graphics pipeline.");
            require(m_targets.initialize(*m_device.Get(), { .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .capacity = 1 }), "Create RTV heap.");
            require(m_depthTargets.initialize(*m_device.Get(), { .type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV, .capacity = 1 }), "Create DSV heap.");
            m_rtv = m_targets.allocate()->cpu.native;
            m_dsv = m_depthTargets.allocate()->cpu.native;
            const std::array<std::uint32_t, 6> indices{ 1, 2, 3, 0, 1, 2 };
            require(m_indices.initialize(*m_device.Get(), m_fence, sizeof(indices))
                && m_indices.write(std::as_bytes(std::span(indices))), "Upload indices.");
            m_indexView = { m_indices.getGpuVirtualAddress(), sizeof(indices), DXGI_FORMAT_R32_UINT };
            D3D12_QUERY_HEAP_DESC queryDescription{ D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 1, 0 };
            require(SUCCEEDED(m_device->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&m_queryHeap))), "Create statistics heap.");
            require(m_pipelineResults.initialize(*m_device.Get(), m_fence, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)), "Create statistics readback.");
        }

        void run(const char* name, const std::vector<TestObject>& objects, const std::uint32_t expectedOccluded,
            const bool instancing = true, const std::uint32_t width = 64, const std::uint32_t height = 64,
            const Matrix& projection = Matrix::Identity, std::optional<D3D12_VIEWPORT> cameraViewport = std::nullopt,
            const std::optional<std::pair<std::uint32_t, std::uint32_t>> hole = std::nullopt, const float depthValue = 0.4f)
        {
            const D3D12_VIEWPORT viewport = cameraViewport.value_or(
                D3D12_VIEWPORT{ 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 });
            DX12GpuOcclusionCulling& culling = m_frames[m_caseCount++ % m_frames.size()];
            GpuOcclusionStatistics completed;
            require(culling.collectCompletedResults(completed), "Collect previous frame.");
            RenderQueue renderQueue;
            std::uint32_t objectID = 0;
            for (const TestObject& object : objects)
            {
                RenderItem item;
                item.vertexBuffer = &m_vertexView;
                item.indexBuffer = &m_indexView;
                item.worldMatrix = Matrix::CreateTranslation(object.center);
                item.worldBounds = AABB(object.center, object.extents);
                item.indexCount = 3;
                item.indexStart = object.indexStart;
                item.baseVertex = object.baseVertex;
                item.objectID = objectID++;
                item.pass = object.pass;
                if (object.skinned)
                    item.skinningPalette = std::make_shared<SkinningPaletteSnapshot>();
                require(renderQueue.submit(item), "Submit test object.");
            }
            renderQueue.sort();
            std::vector<Instance> instances;
            for (const RenderItem& item : renderQueue.getItems())
                instances.push_back({ item.worldMatrix * projection, item.worldMatrix });
            require(m_instances.initialize(*m_device.Get(), m_fence, std::max<std::size_t>(128, instances.size() * sizeof(Instance)))
                && m_instances.write(std::as_bytes(std::span(instances))), "Upload test instances.");

            D3D12_RESOURCE_DESC texture{};
            texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texture.Width = width;
            texture.Height = height;
            texture.DepthOrArraySize = 1;
            texture.MipLevels = 1;
            texture.Format = DXGI_FORMAT_R32_TYPELESS;
            texture.SampleDesc = { 1, 0 };
            texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            const D3D12_CLEAR_VALUE clearDepth{ .Format = DXGI_FORMAT_D32_FLOAT, .DepthStencil = { 1, 0 } };
            require(m_depth.initialize(*m_device.Get(), {
                .description = texture, .initialState = D3D12_RESOURCE_STATE_COPY_DEST, .clearValue = &clearDepth }), "Create depth.");
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = DXGI_FORMAT_D32_FLOAT;
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            m_device->CreateDepthStencilView(m_depth.get(), &dsv, m_dsv);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT depthLayout{};
            std::uint64_t depthBytes = 0;
            m_device->GetCopyableFootprints(&texture, 0, 1, 0, &depthLayout, nullptr, nullptr, &depthBytes);
            std::vector<std::byte> depthData(static_cast<std::size_t>(depthBytes));
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const float value = hole && hole->first == x && hole->second == y ? 1.0f : depthValue;
                    std::memcpy(depthData.data() + y * depthLayout.Footprint.RowPitch + x * sizeof(float), &value, sizeof(value));
                }
            require(m_depthUpload.initialize(*m_device.Get(), m_fence, depthBytes)
                && m_depthUpload.write(depthData), "Upload depth.");
            texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            const D3D12_CLEAR_VALUE clearColor{
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM, .Color = { 0, 0, 0, 1 } };
            require(m_color.initialize(*m_device.Get(), {
                .description = texture, .initialState = D3D12_RESOURCE_STATE_RENDER_TARGET,
                .clearValue = &clearColor }), "Create color.");
            m_device->CreateRenderTargetView(m_color.get(), nullptr, m_rtv);
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT colorLayout{};
            std::uint64_t colorBytes = 0;
            m_device->GetCopyableFootprints(&texture, 0, 1, 0, &colorLayout, nullptr, nullptr, &colorBytes);
            require(m_directImage.initialize(*m_device.Get(), m_fence, colorBytes)
                && m_indirectImage.initialize(*m_device.Get(), m_fence, colorBytes), "Create image readbacks.");
            require(m_list.begin(m_fence), "Begin test command list.");
            ID3D12GraphicsCommandList& uploadNative = *m_list.getForRecording();
            D3D12_TEXTURE_COPY_LOCATION depthSource{};
            depthSource.pResource = m_depthUpload.get();
            depthSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            depthSource.PlacedFootprint = depthLayout;
            D3D12_TEXTURE_COPY_LOCATION depthDestination{};
            depthDestination.pResource = m_depth.get();
            depthDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            uploadNative.CopyTextureRegion(&depthDestination, 0, 0, 0, &depthSource, nullptr);
            require(m_depth.transition(m_list, D3D12_RESOURCE_STATE_DEPTH_WRITE), "Transition depth.");
            require(m_list.close() && m_cullingList.begin(m_fence), "Begin culling pass.");
            m_cullingList.getForRecording()->OMSetRenderTargets(0, nullptr, FALSE, &m_dsv);
            require(culling.record(m_cullingList, *m_device.Get(), m_fence, m_depth, m_hiz, m_cull, renderQueue,
                *m_instances.get(), projection, viewport, instancing), "Record GPU culling.");
            require(m_cullingList.close() && m_colorList.begin(m_fence), "Begin color pass.");
            ID3D12GraphicsCommandList& native = *m_colorList.getForRecording();

            const auto drawImage = [&](const bool indirect, DX12ReadbackBuffer& image)
            {
                require(m_color.transition(m_colorList, D3D12_RESOURCE_STATE_RENDER_TARGET), "Transition color.");
                require(m_graphics.bind(m_colorList), "Bind test pipeline.");
                native.OMSetRenderTargets(1, &m_rtv, FALSE, &m_dsv);
                native.RSSetViewports(1, &viewport);
                const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
                native.RSSetScissorRects(1, &scissor);
                native.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                native.IASetIndexBuffer(&m_indexView);
                const std::array<float, 4> clear{ 0, 0, 0, 1 };
                native.ClearRenderTargetView(m_rtv, clear.data(), 0, nullptr);
                if (indirect)
                    native.BeginQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
                const auto items = renderQueue.getItems();
                for (std::size_t first = 0; first < items.size();)
                {
                    const auto& item = items[first];
                    const auto count = instancing ? renderQueue.getInstanceBatchSize(first) : 1;
                    native.SetGraphicsRootShaderResourceView(0, m_instances.getGpuVirtualAddress() + first * sizeof(Instance));
                    if (!indirect || !culling.execute(native, first))
                        native.DrawIndexedInstanced(item.indexCount, static_cast<UINT>(count), item.indexStart, item.baseVertex, 0);
                    first += count;
                }
                if (indirect)
                {
                    native.EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
                    native.ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1,
                        m_pipelineResults.get(), 0);
                }
                require(m_color.transition(m_colorList, D3D12_RESOURCE_STATE_COPY_SOURCE), "Transition image.");
                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = m_color.get();
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = image.get();
                destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                destination.PlacedFootprint = colorLayout;
                native.CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            };
            drawImage(false, m_directImage);
            drawImage(true, m_indirectImage);
            require(m_colorList.close(), "Close test command list.");
            const std::array lists{ m_list.getForExecution(), m_cullingList.getForExecution(), m_colorList.getForExecution() };
            m_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
            const std::uint64_t fence = m_fence.signal(*m_queue.Get());
            require(fence != 0 && m_list.markSubmitted(fence) && m_cullingList.markSubmitted(fence)
                && m_colorList.markSubmitted(fence) && culling.markSubmitted(fence)
                && m_instances.markUsed(fence) && m_indices.markUsed(fence) && m_depthUpload.markUsed(fence)
                && m_directImage.markPending(fence) && m_indirectImage.markPending(fence)
                && m_pipelineResults.markPending(fence) && m_fence.waitOnCpu(fence), "Submit test and wait.");
            require(culling.collectCompletedResults(completed), "Read GPU culling results.");
            require(completed.occludedInstances == expectedOccluded, name);
            require(completed.instances == static_cast<std::uint32_t>(std::count_if(objects.begin(), objects.end(),
                [](const TestObject& object)
                { return object.pass == RenderPassType::Opaque || object.pass == RenderPassType::AlphaTest; })), "Candidate instance count.");
            D3D12_QUERY_DATA_PIPELINE_STATISTICS pipeline{};
            require(m_pipelineResults.read(std::as_writable_bytes(std::span(&pipeline, 1))), "Read executed vertex count.");
            require(pipeline.IAVertices == (objects.size() - expectedOccluded) * 3, "Hidden Draw must execute no IA vertices.");
            std::vector<std::byte> direct(static_cast<std::size_t>(colorBytes));
            std::vector<std::byte> indirect(static_cast<std::size_t>(colorBytes));
            require(m_directImage.read(direct) && m_indirectImage.read(indirect), "Read images.");
            for (std::uint32_t row = 0; row < height; ++row)
                require(std::memcmp(direct.data() + row * colorLayout.Footprint.RowPitch,
                    indirect.data() + row * colorLayout.Footprint.RowPitch, width * 4) == 0, "Direct/Indirect image mismatch.");
            checkDebugMessages();
            std::printf("PASS %s: %u/%u instances culled, %u/%u batches culled, %llu executed vertices\n",
                name, completed.occludedInstances, completed.instances, completed.occludedBatches, completed.batches, pipeline.IAVertices);
        }

        void finish()
        {
            for (auto& frame : m_frames)
                require(frame.finalize(), "Finalize frame culling.");
            checkDebugMessages();
        }

    private:
        void checkDebugMessages()
        {
            bool failed = false;
            for (UINT64 index = 0; index < m_messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index)
            {
                SIZE_T size = 0;
                require(SUCCEEDED(m_messages->GetMessage(index, nullptr, &size)), "Read debug message size.");
                std::vector<std::byte> data(size);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
                require(SUCCEEDED(m_messages->GetMessage(index, message, &size)), "Read debug message.");
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                {
                    std::fprintf(stderr, "D3D12: %s\n", message->pDescription);
                    failed = true;
                }
            }
            m_messages->ClearStoredMessages();
            require(!failed, "D3D12 debug/GPU validation reported errors or warnings.");
        }

        ComPtr<ID3D12Device> m_device;
        ComPtr<ID3D12InfoQueue> m_messages;
        ComPtr<ID3D12CommandQueue> m_queue;
        DX12Fence m_fence;
        DX12CommandList m_list, m_cullingList, m_colorList;
        DX12ComputePipeline m_hiz, m_cull;
        DX12GraphicsPipeline m_graphics;
        std::array<DX12GpuOcclusionCulling, 2> m_frames;
        DX12DescriptorHeap m_targets, m_depthTargets;
        D3D12_CPU_DESCRIPTOR_HANDLE m_rtv{}, m_dsv{};
        DX12Resource m_depth, m_color;
        DX12UploadBuffer m_depthUpload, m_instances, m_indices;
        DX12ReadbackBuffer m_directImage, m_indirectImage, m_pipelineResults;
        D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
        D3D12_INDEX_BUFFER_VIEW m_indexView{};
        ComPtr<ID3D12QueryHeap> m_queryHeap;
        std::size_t m_caseCount = 0;
    };
}

int main(const int argc, char** argv)
{
    try
    {
        require(argc >= 2, "Pass the test shader output directory.");
        require(Engine::Logger::instance().initialize({
            .useConsole = true, .useFile = false, .useDebugOutput = false }), "Initialize test logger.");
        Tests tests(argv[1], argc >= 3 && std::string_view(argv[2]) == "--warp");
        tests.run("fully hidden: zero Draw", { {} }, 1);
        tests.run("visible", { { Vector3(0, 0, 0.2f) } }, 0);
        tests.run("mixed batch: compact visible instance", { {}, { Vector3(0.3f, 0, 0.2f) } }, 1);
        tests.run("instancing disabled", { {}, { Vector3(-0.3f, 0, 0.2f) } }, 1, false);
        tests.run("near plane crossing", { { Vector3(0, 0, 0.01f) } }, 0);
        tests.run("skinning stays visible", { { Vector3(0, 0, 0.8f), Vector3(0.15f), true } }, 0);
        tests.run("degenerate bounds stay visible", { { Vector3(0, 0, 0.8f), Vector3::Zero } }, 0);
        tests.run("depth equality stays visible", { { Vector3(0, 0, 0.4f), Vector3(0.15f) } }, 0);
        tests.run("transparent remains direct", { { Vector3(0, 0, 0.8f), Vector3(0.15f), false, RenderPassType::Transparent } }, 0);
        tests.run("alpha test can be culled", { { Vector3(0, 0, 0.8f), Vector3(0.15f), false, RenderPassType::AlphaTest } }, 1);
        tests.run("shadow and depth remain direct", {
            { Vector3(0, 0, 0.8f), Vector3(0.15f), false, RenderPassType::DepthOnly },
            { Vector3(0, 0, 0.8f), Vector3(0.15f), false, RenderPassType::Shadow }, {} }, 1);
        tests.run("NPOT padded depth", { {} }, 1, true, 63, 37);
        tests.run("hole prevents occlusion", { {} }, 0, true, 63, 37, Matrix::Identity, std::nullopt, std::pair{ 31u, 18u });
        tests.run("last NPOT texel is not dropped", { { Vector3(0, 0, 0.8f), Vector3(1, 1, 0.05f) } },
            0, true, 63, 37, Matrix::Identity, std::nullopt, std::pair{ 62u, 36u });
        tests.run("offset viewport", { {}, { Vector3(0.3f, 0, 0.2f) } }, 1, true, 127, 71,
            Matrix::Identity, D3D12_VIEWPORT{ 17.5f, 9.25f, 61.5f, 37.25f, 0, 1 });
        tests.run("one-pixel target", { {} }, 1, true, 1, 1);
        tests.run("empty queue", {}, 0);
        std::vector<TestObject> largeBatch(257);
        for (std::size_t index = 0; index < largeBatch.size(); index += 3)
            largeBatch[index].center.z = 0.2f;
        tests.run("multi-wave compaction", largeBatch, 171);
        tests.run("camera movement has no stale results", { {} }, 0, true, 64, 64, Matrix::CreateTranslation(0, 0, -0.6f));
        const Matrix perspective(DirectX::XMMatrixPerspectiveFovLH(1.0f, 1.0f, 0.1f, 100.0f));
        tests.run("perspective depth", { { Vector3(0, 0, 5) } }, 1, true, 64, 64, perspective);
        tests.run("camera inside bounds", { { Vector3(0, 0, 0.1f), Vector3(1) } }, 0, true, 64, 64, perspective);
        tests.run("signed base vertex and index offsets", {
            { Vector3(-0.3f, 0, 0.2f), Vector3(0.15f), false, RenderPassType::Opaque, 0, -1 },
            { Vector3(0.3f, 0, 0.2f) }, {} }, 1);
        tests.run("dispatch chunk boundary", std::vector<TestObject>(65536), 65536, false, 1, 1);
        tests.finish();
        std::puts("All GPU occlusion tests passed with D3D12 debug layer and GPU-based validation.");
        Engine::Logger::instance().finalize();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "FAIL: %s\n", exception.what());
        return 1;
    }
}
