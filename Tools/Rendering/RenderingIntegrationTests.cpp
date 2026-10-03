#include "Pch.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Model\ModelManager.h"
#include "Graphics\DirectX12\ModelCommandRecorder.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Graphics\Shader\Shader.h"
#include "Graphics\Texture\TextureManager.h"
#include <d3d12sdklayers.h>

int runCommandRecordingRangeTests();

namespace
{
    using namespace Engine;
    using Microsoft::WRL::ComPtr;

    void require(const bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void checkMessages(ID3D12InfoQueue& messages)
    {
        for (UINT64 index = 0; index < messages.GetNumStoredMessages(); ++index)
        {
            SIZE_T size = 0;
            require(SUCCEEDED(messages.GetMessage(index, nullptr, &size)), "Debug message size failed");
            std::vector<std::byte> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            require(SUCCEEDED(messages.GetMessage(index, message, &size)), "Debug message read failed");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                std::fprintf(stderr, "DX12 validation: %s\n", message->pDescription);
                throw std::runtime_error("DX12 debug layer reported a warning or error");
            }
        }
    }

    struct GpuFixture
    {
        ComPtr<IDXGIFactory4> factory;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12InfoQueue> messages;
        DX12CommandQueue queue;
        DX12Fence fence;
        DX12DescriptorHeap rtvHeap;
        DX12DescriptorHeap dsvHeap;
        DX12DescriptorHeap textureHeap;
        DX12Resource color;
        DX12Resource depth;
        DX12Resource shadow;
        DX12UploadBuffer vertices;
        DX12UploadBuffer indices;
        DX12UploadBuffer instances;
        DX12UploadBuffer constants;
        DX12Shader vertexShader;
        DX12Shader pixelShader;
        DX12Shader queryShader;
        DX12GraphicsPipeline depthPipeline;
        DX12GraphicsPipeline colorPipeline;
        DX12GraphicsPipeline transparentPipeline;
        DX12GraphicsPipeline queryPipeline;
        std::array<DX12ModelCommandRecorder, 2> recorders;
        std::array<DX12CommandList, 2> beginLists;
        std::array<DX12CommandList, 2> endLists;
        std::array<DX12CommandList, 2> queryLists;
        std::array<DX12ReadbackBuffer, 2> readbacks;
        std::array<DX12OcclusionQueries, 2> queries;
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        D3D12_INDEX_BUFFER_VIEW indexView{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        DX12ModelRecordingState state;
        std::vector<RenderItem> items;
        std::vector<DX12PreparedModelDraw> draws;
        RenderQueue renderQueue;
        std::uint64_t readbackSize = 0;
        std::uint32_t maximumThreads = 0;
        std::uint32_t completedQueryFrames = 0;

        ~GpuFixture()
        {
            if (queue.get() != nullptr && fence.get() != nullptr)
            {
                const auto value = fence.signal(*queue.get());
                if (value != 0)
                    fence.waitOnCpu(value);
            }
        }

        void initialize(const std::filesystem::path& shaderDirectory, const bool warp)
        {
            ComPtr<ID3D12Debug1> debug;
            require(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))), "DX12 debug layer unavailable");
            debug->EnableDebugLayer();
            debug->SetEnableGPUBasedValidation(TRUE);
            require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "DXGI factory creation failed");
            if (warp)
            {
                ComPtr<IDXGIAdapter> adapter;
                require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))), "WARP unavailable");
                require(SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device))), "WARP device creation failed");
            }
            else
            {
                require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device))), "Hardware DX12 device creation failed");
            }
            require(SUCCEEDED(device.As(&messages)), "DX12 InfoQueue unavailable");
            require(queue.initialize(*device.Get(), DX12CommandQueueType::DIRECT)
                && fence.initialize(*device.Get()), "Queue/fence initialization failed");
            require(rtvHeap.initialize(*device.Get(), { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false })
                && dsvHeap.initialize(*device.Get(), { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false })
                && textureHeap.initialize(*device.Get(), { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 5, true }),
                "Descriptor heaps failed");

            const auto createTarget = [&](DX12Resource& resource, const UINT size, const bool isDepth)
                {
                    D3D12_RESOURCE_DESC desc{};
                    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                    desc.Width = size;
                    desc.Height = size;
                    desc.DepthOrArraySize = 1;
                    desc.MipLevels = 1;
                    desc.Format = isDepth ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
                    desc.SampleDesc = { 1, 0 };
                    desc.Flags = isDepth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                    D3D12_CLEAR_VALUE clear{};
                    clear.Format = desc.Format;
                    if (isDepth)
                        clear.DepthStencil.Depth = 1.0f;
                    require(resource.initialize(*device.Get(), {
                        .description = desc,
                        .initialState = isDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET,
                        .clearValue = &clear,
                    }), "Render target creation failed");
                };
            createTarget(color, 64, false);
            createTarget(depth, 64, true);
            createTarget(shadow, 2048, true);
            state.colorTarget = rtvHeap.allocate()->cpu.native;
            state.depthTarget = dsvHeap.allocate()->cpu.native;
            state.shadowTarget = dsvHeap.allocate()->cpu.native;
            device->CreateRenderTargetView(color.get(), nullptr, state.colorTarget);
            device->CreateDepthStencilView(depth.get(), nullptr, state.depthTarget);
            device->CreateDepthStencilView(shadow.get(), nullptr, state.shadowTarget);
            state.textureHeap = textureHeap.get();
            state.viewport = { 8.0f, 8.0f, 48.0f, 48.0f, 0.0f, 1.0f };
            state.scissor = { 8, 8, 56, 56 };
            state.shadowViewport = { 0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f };
            state.shadowScissor = { 0, 0, 2048, 2048 };
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5> textures{};
            for (auto& texture : textures)
            {
                const auto allocation = textureHeap.allocate();
                require(allocation.has_value() && allocation->gpu.has_value(), "Texture descriptor allocation failed");
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Texture2D.MipLevels = 1;
                device->CreateShaderResourceView(nullptr, &srv, allocation->cpu.native);
                texture = allocation->gpu->native;
            }
            require(vertexShader.load({ shaderDirectory / "Recording_vs.cso" })
                && pixelShader.load({ shaderDirectory / "Recording_ps.cso" })
                && queryShader.load({ "Assets\\Shaders\\Compiled\\Occlusion_vsMain_vs.cso" }), "Test shaders failed");
            std::array<D3D12_ROOT_PARAMETER, 9> roots{};
            roots[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            roots[0].Descriptor.ShaderRegister = 5;
            roots[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            roots[1].Descriptor.ShaderRegister = 1;
            roots[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            roots[2].Descriptor.ShaderRegister = 2;
            std::array<D3D12_DESCRIPTOR_RANGE, 5> textureRanges{};
            for (UINT index = 0; index < textureRanges.size(); ++index)
            {
                textureRanges[index] = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, index, 0, 0 };
                roots[3 + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                roots[3 + index].DescriptorTable = { 1, &textureRanges[index] };
            }
            roots[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            roots[8].Constants = { 3, 0, 17 };
            const std::array layout = {
                D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };
            DX12GraphicsPipelineConfig config{
                .vertexShader = &vertexShader, .pixelShader = &pixelShader,
                .inputLayout = layout, .rootParameters = roots,
                .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
                .depthComparison = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                .enableDepthWrite = false, .cullMode = D3D12_CULL_MODE_NONE,
            };
            require(colorPipeline.initialize(*device.Get(), config), "Color pipeline failed");
            config.enableAlphaBlend = true;
            require(transparentPipeline.initialize(*device.Get(), config), "Transparent pipeline failed");
            config.enableAlphaBlend = false;
            config.enableDepthWrite = true;
            config.pixelShader = nullptr;
            config.renderTargetFormat = DXGI_FORMAT_UNKNOWN;
            require(depthPipeline.initialize(*device.Get(), config), "Depth pipeline failed");
            const std::array queryRoot = {
                D3D12_ROOT_PARAMETER{ D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_VERTEX },
            };
            auto queryParameters = queryRoot;
            queryParameters[0].Constants = { 0, 0, 5 };
            config.vertexShader = &queryShader;
            config.inputLayout = {};
            config.rootParameters = queryParameters;
            config.enableDepthWrite = false;
            require(queryPipeline.initialize(*device.Get(), config), "Query pipeline failed");

            const std::array positions = { Vector3(-1.0f, -1.0f, 0.5f), Vector3(-1.0f, 3.0f, 0.5f),
                Vector3(3.0f, -1.0f, 0.5f) };
            std::array<std::uint32_t, 300> indexData{};
            // One visible triangle plus degenerate primitives exercises query thresholds without excessive WARP overdraw.
            indexData[1] = 1;
            indexData[2] = 2;
            struct Instance { Matrix viewProjection; Matrix world; };
            const Instance instance{ Matrix::Identity, Matrix::Identity };
            require(vertices.initialize(*device.Get(), fence, sizeof(positions))
                && vertices.write(std::as_bytes(std::span(positions)))
                && indices.initialize(*device.Get(), fence, sizeof(indexData))
                && indices.write(std::as_bytes(std::span(indexData)))
                && instances.initialize(*device.Get(), fence, sizeof(instance) * 3)
                && constants.initialize(*device.Get(), fence, 256), "Upload buffers failed");
            for (UINT index = 0; index < 3; ++index)
                require(instances.write(std::as_bytes(std::span(&instance, 1)), index * sizeof(instance)), "Instance upload failed");
            vertexView = { vertices.getGpuVirtualAddress(), sizeof(positions), sizeof(Vector3) };
            indexView = { indices.getGpuVirtualAddress(), sizeof(indexData), DXGI_FORMAT_R32_UINT };
            const auto desc = color.getDescription();
            device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackSize);
            for (UINT frame = 0; frame < 2; ++frame)
                require(beginLists[frame].initialize(*device.Get(), DX12CommandQueueType::DIRECT)
                    && endLists[frame].initialize(*device.Get(), DX12CommandQueueType::DIRECT)
                    && queryLists[frame].initialize(*device.Get(), DX12CommandQueueType::DIRECT)
                    && readbacks[frame].initialize(*device.Get(), fence, readbackSize), "Frame resources failed");

            constexpr std::size_t drawsPerPass = 129;
            items.resize(drawsPerPass * 5);
            draws.reserve(items.size());
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                RenderItem& item = items[index];
                item.vertexBuffer = &vertexView;
                item.indexBuffer = &indexView;
                item.indexCount = static_cast<std::uint32_t>(indexData.size());
                item.pass = static_cast<RenderPassType>(index / drawsPerPass);
                item.worldBounds = AABB(Vector3(0.0f, 0.0f, index % 2 == 0 ? 0.3f : 0.8f),
                    Vector3(0.2f, 0.2f, 0.01f));
                require(item.materialProperties.setVector4(MaterialParameters::BaseColor,
                    index % 2 == 0 ? Vector4(0.8f, 0.1f, 0.2f, 0.01f) : Vector4(0.1f, 0.8f, 0.2f, 0.01f)),
                    "Property block failed");
                require(renderQueue.submit(item), "Query queue submission failed");
                const auto* pipeline = item.pass <= RenderPassType::Shadow ? &depthPipeline
                    : item.pass == RenderPassType::Transparent ? &transparentPipeline : &colorPipeline;
                draws.push_back({
                    .item = &item, .pipeline = pipeline, .itemIndex = index,
                    .instanceCount = item.pass == RenderPassType::Transparent ? 1u : 3u,
                    .instances = instances.getGpuVirtualAddress(), .material = constants.getGpuVirtualAddress(),
                    .bones = constants.getGpuVirtualAddress(), .textures = textures,
                });
            }
        }

        std::vector<std::byte> render(const UINT frame, const bool parallel, const bool occlusion)
        {
            UINT completed = 0;
            UINT hidden = 0;
            require(queries[frame].collectCompletedResults(completed, hidden), "Query result collection failed");
            if (completed != 0)
            {
                require(completed == 258 && hidden == 129, "Unexpected completed occlusion results");
                ++completedQueryFrames;
            }
            auto& begin = beginLists[frame];
            auto& end = endLists[frame];
            require(begin.begin(fence) && color.transition(begin, D3D12_RESOURCE_STATE_RENDER_TARGET), "Begin frame failed");
            const float clear[4]{};
            auto* native = begin.getForRecording();
            native->ClearRenderTargetView(state.colorTarget, clear, 0, nullptr);
            native->ClearDepthStencilView(state.depthTarget, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            native->ClearDepthStencilView(state.shadowTarget, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            require(begin.close(), "Begin frame close failed");
            state.queries = nullptr;
            if (occlusion)
            {
                auto& query = queryLists[frame];
                require(query.begin(fence), "Query list begin failed");
                native = query.getForRecording();
                native->OMSetRenderTargets(0, nullptr, FALSE, &state.depthTarget);
                native->RSSetViewports(1, &state.viewport);
                native->RSSetScissorRects(1, &state.scissor);
                require(queries[frame].record(query, *device.Get(), fence, queryPipeline, renderQueue,
                    Matrix::Identity, Vector2(48.0f, 48.0f), false) && query.close(), "Query recording failed");
                require(queries[frame].getQueryCount() != 0, "Occlusion test did not generate queries");
                state.queries = &queries[frame];
            }
            auto& recorder = recorders[frame];
            require(recorder.record(*device.Get(), fence, draws, state, parallel), "Model recording failed");
            const auto& stats = recorder.getStatistics();
            require(stats.drawCallCount == draws.size() && stats.instanceCount == 129 * 13, "Incorrect recording statistics");
            const std::size_t concurrency = parallel && JobSystem::instance().isInitialized()
                ? static_cast<std::size_t>(JobSystem::instance().getWorkerCount()) + 1 : 1;
            require(recorder.getRanges().size() == std::min(std::size_t{ 3 }, concurrency) * 5,
                "Incorrect command list partition count");
            maximumThreads = std::max(maximumThreads, stats.recordingThreadCount);
            if (!parallel)
                require(stats.recordingThreadCount == 1, "Serial recording used multiple threads");
            std::vector<ID3D12CommandList*> execution{ begin.getForExecution() };
            bool inserted = !occlusion;
            const auto ranges = recorder.getRanges();
            for (std::size_t index = 0; index < ranges.size(); ++index)
            {
                if (!inserted && draws[ranges[index].first].item->pass >= RenderPassType::Opaque)
                {
                    execution.push_back(queryLists[frame].getForExecution());
                    inserted = true;
                }
                execution.push_back(recorder.getForExecution(index));
            }
            require(end.begin(fence) && color.transition(end, D3D12_RESOURCE_STATE_COPY_SOURCE), "Readback transition failed");
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = color.get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = readbacks[frame].get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = footprint;
            end.getForRecording()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            require(end.close(), "Readback list close failed");
            execution.push_back(end.getForExecution());
            queue.execute(execution);
            const auto value = fence.signal(*queue.get());
            require(value != 0 && begin.markSubmitted(value) && end.markSubmitted(value)
                && recorder.markSubmitted(value) && readbacks[frame].markPending(value), "Frame fence marking failed");
            if (occlusion)
                require(queryLists[frame].markSubmitted(value) && queries[frame].markSubmitted(value), "Query fence marking failed");
            require(fence.waitOnCpu(value), "GPU completion failed");
            std::vector<std::byte> pixels(static_cast<std::size_t>(readbackSize));
            require(readbacks[frame].read(pixels), "Readback failed");
            require(pixels[32 * footprint.Footprint.RowPitch + 32 * 4] != std::byte{},
                "No visible pixels; image comparison would be vacuous");
            return pixels;
        }

    };

    void testRenderer()
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = L"RenderingIntegrationTests";
        require(RegisterClassW(&windowClass) != 0, "Test window class failed");
        const HWND hwnd = CreateWindowW(windowClass.lpszClassName, L"Rendering tests", WS_OVERLAPPEDWINDOW,
            0, 0, 320, 240, nullptr, nullptr, instance, nullptr);
        require(hwnd != nullptr, "Test window creation failed");
        struct WindowOwner
        {
            HWND hwnd;
            HINSTANCE instance;
            ~WindowOwner()
            {
                DestroyWindow(hwnd);
                UnregisterClassW(L"RenderingIntegrationTests", instance);
            }
        } window{ hwnd, instance };
        DX12Renderer renderer;
        require(renderer.initialize(hwnd, 320, 240), "Renderer initialization failed");
        ComPtr<ID3D12Device> rendererDevice;
        ComPtr<ID3D12InfoQueue> rendererMessages;
        require(SUCCEEDED(TextureManager::instance().getDescriptorHeap().get()->GetDevice(
            IID_PPV_ARGS(&rendererDevice))) && SUCCEEDED(rendererDevice.As(&rendererMessages)),
            "Renderer validation queue unavailable");
        struct RendererOwner
        {
            DX12Renderer& renderer;
            ~RendererOwner() { renderer.finalize(); }
        } owner{ renderer };
        ModelResource model;
        MeshResource mesh;
        mesh.vertices.resize(3);
        mesh.vertices[0].position = Vector3(-0.5f, -0.5f, 0.5f);
        mesh.vertices[1].position = Vector3(0.0f, 0.5f, 0.5f);
        mesh.vertices[2].position = Vector3(0.5f, -0.5f, 0.5f);
        for (auto& vertex : mesh.vertices)
            vertex.boneWeights.x = 1.0f;
        for (UINT index = 0; index < 300; ++index)
            mesh.indices.push_back(index % 3);
        mesh.boundingBox = AABB(Vector3(0.0f, 0.0f, 0.5f), Vector3(0.5f, 0.5f, 0.01f));
        model.boundingBox = mesh.boundingBox;
        model.meshes.push_back(std::move(mesh));
        const auto modelHandle = ModelManager::instance().create(std::move(model));
        require(modelHandle.isValid(), "Model registration failed");
        std::array<MaterialHandle, 3> materials;
        for (UINT type = 0; type < 3; ++type)
        {
            MaterialAsset material;
            material.renderState.surfaceType = static_cast<MaterialSurfaceType>(type);
            material.baseColor = Vector4(0.3f, 0.6f, 0.9f, type == 2 ? 0.1f : 1.0f);
            materials[type] = MaterialManager::instance().create(std::move(material));
            require(materials[type].isValid(), "Material registration failed");
        }
        auto palette = std::make_shared<SkinningPaletteSnapshot>();
        palette->jointCount = 1;
        for (auto& matrix : palette->constants.boneMatrices)
            matrix = Matrix::Identity;
        for (auto& matrix : palette->constants.boneNormalMatrices)
            matrix = Matrix::Identity;
        palette->constants.boneMatrices[0] = Matrix::CreateTranslation(0.05f, 0.0f, 0.0f);
        for (UINT frame = 0; frame < 16; ++frame)
        {
            renderer.setParallelModelRecordingEnabled(frame % 2 == 0);
            renderer.setModelInstancingEnabled(frame % 4 < 2);
            renderer.setOcclusionCullingEnabled(frame % 8 < 4);
            renderer.setShadowViewProjection(Matrix::Identity);
            if (frame == 8)
            {
                JobSystem::instance().finalize();
                require(JobSystem::instance().initialize(2), "Two-worker JobSystem initialization failed");
                require(renderer.resize(400, 300), "Renderer resize failed");
            }
            for (UINT index = 0; index < 195; ++index)
            {
                ModelRenderSubmission submission;
                submission.model = modelHandle;
                submission.materials = { materials[index % 3] };
                submission.objectID = index;
                submission.worldBounds = AABB(Vector3(0.0f, 0.0f, 0.5f), Vector3(0.5f, 0.5f, 0.01f));
                if (index % 7 == 0)
                    submission.skinningPalette = palette;
                if (index % 11 == 0)
                    require(submission.materialProperties.setFloat(MaterialParameters::Roughness, 0.4f),
                        "Renderer property override failed");
                ModelRenderSubmissionQueue::instance().submit(std::move(submission));
            }
            bool succeeded = false;
            JobCounter counter;
            JobSystem::instance().schedule([&] { succeeded = renderer.render(); }, &counter);
            while (!counter.isComplete())
                yieldThread();
            require(succeeded, "Renderer frame failed");
            const auto& stats = renderer.getStatistics();
            require(stats.visibleObjects == 195 && stats.renderItemCount == 455
                && stats.instanceCount == 455 && stats.drawCallCount != 0, "Renderer lost submissions");
        }
        require(renderer.render() && renderer.getStatistics().drawCallCount == 0, "Empty frame failed");
        require(renderer.resize(320, 240), "Final GPU completion wait failed");
        checkMessages(*rendererMessages.Get());
        rendererMessages.Reset();
        rendererDevice.Reset();
        ModelRenderSubmission invalid;
        invalid.model = modelHandle;
        invalid.materials = { materials[0] };
        invalid.skinningPalette = std::make_shared<SkinningPaletteSnapshot>();
        ModelRenderSubmissionQueue::instance().submit(std::move(invalid));
        require(!renderer.render() && !renderer.render(), "A failed frame was silently reused");
        require(renderer.finalize(), "Renderer shutdown failed");
        require(renderer.initialize(hwnd, 320, 240) && renderer.render() && renderer.finalize(), "Renderer reinitialization failed");
        ModelManager::instance().clear();
        MaterialManager::instance().clear();
        std::puts("PASS: single-/two-worker nested renderer frames, instancing, skinning snapshots, overrides, occlusion toggles, resize, empty frame, fail-stop and reinitialization.");
    }
}

int main(int argc, char** argv)
{
    if (runCommandRecordingRangeTests() != 0)
        return 1;
    require(Logger::instance().initialize(), "Logger initialization failed");
    int result = 0;
    try
    {
        require(JobSystem::instance().initialize(3), "JobSystem initialization failed");
        {
            GpuFixture gpu;
            gpu.initialize(std::filesystem::absolute(argv[0]).parent_path(), argc > 1 && std::string_view(argv[1]) == "--warp");
            for (const bool occlusion : { false, true })
            {
                const auto reference = gpu.render(0, false, occlusion);
                for (UINT frame = 0; frame < 6; ++frame)
                    require(gpu.render(frame % 2, true, occlusion) == reference, "Parallel image differs from serial image");
            }
            require(gpu.maximumThreads >= 2, "Recording never ran on multiple threads");
            require(gpu.completedQueryFrames >= 4, "Occlusion readback was not exercised");
            JobSystem::instance().finalize();
            const auto fallback = gpu.render(0, false, false);
            require(gpu.render(1, true, false) == fallback
                && gpu.recorders[1].getStatistics().recordingThreadCount == 1, "Uninitialized JobSystem fallback failed");
            checkMessages(*gpu.messages.Get());
            std::printf("PASS: byte-exact serial/parallel GPU images, depth/shadow/color/transparent order, same-frame predicates, two frame slots; %u recording threads; no DX12 validation warnings/errors.\n",
                gpu.maximumThreads);
        }
        require(JobSystem::instance().initialize(1), "Single-worker JobSystem initialization failed");
        testRenderer();
    }
    catch (const std::exception& exception)
    {
        std::fprintf(stderr, "FAIL: %s\n", exception.what());
        result = 1;
    }
    JobSystem::instance().finalize();
    Logger::instance().finalize();
    return result;
}
