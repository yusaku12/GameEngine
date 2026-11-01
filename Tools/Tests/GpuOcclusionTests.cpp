#include "Pch.h"
#include <d3d12sdklayers.h>
#include "Graphics\DirectX12\GpuOcclusionCulling.h"
#include "Graphics\DirectX12\Pipeline.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\Shader\Shader.h"
#include "Graphics\Renderer\RenderQueue.h"
#include "Graphics\Renderer\SkinningBounds.h"
#include "Graphics\Camera\CameraUtils.h"

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
        std::shared_ptr<const SkinningPaletteSnapshot> palette;
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
            std::array<D3D12_ROOT_PARAMETER, 2> roots{};
            roots[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            roots[0].Descriptor.ShaderRegister = 5;
            roots[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            roots[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            roots[1].Descriptor.ShaderRegister = 0;
            roots[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            require(m_graphics.initialize(*m_device.Get(), {
                .vertexShader = &vertex, .pixelShader = &pixel,
                .rootParameters = roots,
                .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
                .depthComparison = D3D12_COMPARISON_FUNC_LESS_EQUAL,
                .enableDepthWrite = false, .cullMode = D3D12_CULL_MODE_NONE }), "Create graphics pipeline.");
            require(m_equalGraphics.initialize(*m_device.Get(), {
                .vertexShader = &vertex, .pixelShader = &pixel,
                .rootParameters = roots,
                .depthStencilFormat = DXGI_FORMAT_D32_FLOAT,
                .depthComparison = D3D12_COMPARISON_FUNC_EQUAL,
                .enableDepthWrite = false, .cullMode = D3D12_CULL_MODE_NONE }), "Create production depth-equality pipeline.");
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
            const std::optional<std::pair<std::uint32_t, std::uint32_t>> hole = std::nullopt,
            const float depthValue = 0.4f, const bool equalDepth = false)
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
                {
                    item.skinningPalette = std::make_shared<SkinningPaletteSnapshot>();
                }
                if (object.palette != nullptr)
                {
                    item.skinningPalette = object.palette;
                    const std::array positions{ Vector3(-0.1f, -0.1f, 0), Vector3(0, 0.1f, 0), Vector3(0.1f, -0.1f, 0) };
                    std::array<ModelVertex, 3> vertices{};
                    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
                    {
                        vertices[vertex].position = positions[vertex];
                        vertices[vertex].boneWeights = Vector4(1, 0, 0, 0);
                    }
                    SkinningBounds poseBounds;
                    require(poseBounds.initialize(vertices), "Build animated triangle bounds.");
                    item.animationBoundsValid = poseBounds.calculate(*object.palette, item.worldMatrix, item.worldBounds);
                    require(item.animationBoundsValid, "Calculate animated triangle bounds.");
                    // The test graphics shader draws rigid triangles; fold the same single-bone transform into its instance.
                    item.worldMatrix = object.palette->constants.boneMatrices[0] * item.worldMatrix;
                }
                require(renderQueue.submit(item), "Submit test object.");
            }
            renderQueue.sort();
            std::vector<Instance> instances;
            for (const RenderItem& item : renderQueue.getItems())
                instances.push_back({ item.worldMatrix * projection, item.worldMatrix });
            require(m_instances.initialize(*m_device.Get(), m_fence, std::max<std::size_t>(128, instances.size() * sizeof(Instance)))
                && m_instances.write(std::as_bytes(std::span(instances))), "Upload test instances.");
            D3D12_RESOURCE_DESC recordedDescription = m_instances.get()->GetDesc();
            recordedDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            require(m_recordedInstances.initialize(*m_device.Get(), {
                .description = recordedDescription })
                && m_recordedReadback.initialize(*m_device.Get(), m_fence, recordedDescription.Width),
                "Create actual vertex-shader instance capture.");

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
            if (m_caseCount == 1)
            {
                for (const D3D12_VIEWPORT invalid : {
                    D3D12_VIEWPORT{ 0, 0, static_cast<float>(width) + 1, static_cast<float>(height), 0, 1 },
                    D3D12_VIEWPORT{ 0, 0, static_cast<float>(width), static_cast<float>(height) + 1, 0, 1 },
                    D3D12_VIEWPORT{ -1, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 },
                    D3D12_VIEWPORT{ 0, 0, std::numeric_limits<float>::quiet_NaN(), static_cast<float>(height), 0, 1 } })
                    require(!culling.record(m_cullingList, *m_device.Get(), m_fence, m_depth, m_hiz, m_cull,
                        renderQueue, *m_instances.get(), projection, invalid, instancing),
                        "Invalid viewport must still be rejected.");
            }
            require(culling.record(m_cullingList, *m_device.Get(), m_fence, m_depth, m_hiz, m_cull, renderQueue,
                *m_instances.get(), projection, viewport, instancing), "Record GPU culling.");
            require(m_cullingList.close() && m_colorList.begin(m_fence), "Begin color pass.");
            ID3D12GraphicsCommandList& native = *m_colorList.getForRecording();

            const auto drawImage = [&](const bool indirect, DX12ReadbackBuffer& image)
            {
                require(m_color.transition(m_colorList, D3D12_RESOURCE_STATE_RENDER_TARGET), "Transition color.");
                require(m_recordedInstances.transition(m_colorList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), "Transition instance capture.");
                require((equalDepth ? m_equalGraphics : m_graphics).bind(m_colorList), "Bind test pipeline.");
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
                    native.SetGraphicsRootUnorderedAccessView(1, m_recordedInstances.get()->GetGPUVirtualAddress() + first * sizeof(Instance));
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
                require(m_recordedInstances.transition(m_colorList, D3D12_RESOURCE_STATE_COPY_SOURCE), "Transition captured instances.");
                if (indirect)
                    native.CopyBufferRegion(m_recordedReadback.get(), 0, m_recordedInstances.get(), 0, recordedDescription.Width);
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
                && m_recordedReadback.markPending(fence)
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
            if (equalDepth)
            {
                std::vector<Instance> captured(instances.size());
                require(m_recordedReadback.read(std::as_writable_bytes(std::span(captured))), "Read actual shader instance sequence.");
                std::size_t destination = 0;
                for (const Instance& instance : instances)
                    if (instance.world._43 == depthValue)
                    {
                        require(std::memcmp(&captured[destination], &instance, sizeof(Instance)) == 0,
                            "Every visible instance must retain its source order and all 128 constant bytes.");
                        ++destination;
                    }
                require(destination == instances.size() - expectedOccluded, "Stable-order reference must cover every visible instance.");
            }
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

        void testCameraCulling()
        {
            const Matrix orthographic = CameraUtils::buildOrthographic(5, 1, 0.1f, 100);
            const CameraFrustum orthographicFrustum = CameraUtils::buildWorldFrustum(orthographic, Matrix::Identity);
            require(orthographicFrustum.valid, "Orthographic clip planes must be valid.");
            const AABB visible(Vector3(1, 0, 1), Vector3(0.1f));
            require(isVisible(orthographicFrustum, visible), "Orthographic interior bounds must remain visible.");
            RenderItem item;
            item.vertexBuffer = &m_vertexView;
            item.indexBuffer = &m_indexView;
            item.indexCount = 3;
            item.worldBounds = visible;
            RenderQueue queue;
            require(queue.submit(item, &orthographicFrustum), "Production queue must retain orthographic interior bounds.");
            const std::array outside{
                Vector3(-6, 0, 1), Vector3(6, 0, 1), Vector3(0, -6, 1), Vector3(0, 6, 1),
                Vector3(0, 0, -1), Vector3(0, 0, 101) };
            for (const Vector3& center : outside)
            {
                item.worldBounds = AABB(center, Vector3(0.1f));
                require(!queue.submit(item, &orthographicFrustum), "Orthographic bounds outside each plane must be culled.");
                require(!isVisible(orthographicFrustum, BoundingSphere(center, 0.1f)), "Orthographic outside sphere.");
            }
            for (const Vector3& center : { Vector3(-5, 0, 1), Vector3(5, 0, 1), Vector3(0, -5, 1),
                Vector3(0, 5, 1), Vector3(0, 0, 0.1f), Vector3(0, 0, 100) })
            {
                require(isVisible(orthographicFrustum, AABB(center, Vector3(0.1f))), "Clip-plane intersecting bounds stay visible.");
                require(isVisible(orthographicFrustum, BoundingSphere(center, 0.1f)), "Clip-plane intersecting sphere stays visible.");
            }
            std::mt19937 random(803);
            std::uniform_real_distribution<float> coordinate(-20, 20);
            std::uniform_real_distribution<float> depth(-5, 110);
            std::size_t checked = 0;
            for (const Matrix& cameraWorld : { Matrix::Identity,
                Matrix::CreateRotationY(0.7f) * Matrix::CreateTranslation(17, -4, 11),
                Matrix::CreateScale(-2, 0.5f, 3) * Matrix::CreateRotationX(-0.4f)
                    * Matrix::CreateTranslation(-10, 8, 3) })
                for (Matrix projection : { orthographic, CameraUtils::buildPerspective(65, 1.3f, 0.1f, 100) })
                {
                    projection._31 += 0.003f;
                    projection._32 -= 0.005f;
                    const CameraFrustum frustum = CameraUtils::buildWorldFrustum(projection, cameraWorld);
                    require(frustum.valid, "Transformed perspective/orthographic clip planes.");
                    const Matrix viewProjection = cameraWorld.Invert() * projection;
                    for (unsigned index = 0; index < 4096; ++index)
                    {
                        const Vector3 center = Vector3::Transform(Vector3(coordinate(random), coordinate(random), depth(random)), cameraWorld);
                        const AABB bounds(center, Vector3(0.1f));
                        std::array<bool, 6> outsidePlane;
                        outsidePlane.fill(true);
                        bool touchesPlane = false;
                        std::array<Vector3, AABB::CORNER_COUNT> corners;
                        bounds.GetCorners(corners.data());
                        for (const Vector3& corner : corners)
                        {
                            const Vector4 clip = Vector4::Transform(Vector4(corner.x, corner.y, corner.z, 1), viewProjection);
                            const std::array distances{ clip.w + clip.x, clip.w - clip.x, clip.w + clip.y,
                                clip.w - clip.y, clip.z, clip.w - clip.z };
                            for (std::size_t plane = 0; plane < distances.size(); ++plane)
                            {
                                outsidePlane[plane] = outsidePlane[plane] && distances[plane] < 0;
                                touchesPlane = touchesPlane || std::abs(distances[plane]) < 0.005f;
                            }
                        }
                        if (!touchesPlane)
                        {
                            const bool expected = std::none_of(outsidePlane.begin(), outsidePlane.end(), [](bool value) { return value; });
                            require(isVisible(frustum, bounds) == expected, "Clip-plane culling must match vertex clip inequalities.");
                            ++checked;
                        }
                    }
                }
            require(checked > 12000, "Camera culling reference must cover over 12000 bounds.");
            const CameraFrustum invalid = CameraUtils::buildWorldFrustum(orthographic, Matrix::CreateScale(0));
            require(!invalid.valid && isVisible(invalid, visible), "Singular camera must log and bypass culling.");
            Matrix nonfinite = orthographic;
            nonfinite._11 = std::numeric_limits<float>::quiet_NaN();
            require(!CameraUtils::buildWorldFrustum(nonfinite, Matrix::Identity).valid, "Nonfinite projection must disable culling.");
            std::printf("PASS camera clip planes: %zu AABBs, six orthographic faces, spheres and queue submission.\n", checked);

            std::size_t viewportChecks = 0;
            for (const Vector2& size : { Vector2(1, 1), Vector2(63, 37), Vector2(127, 71),
                Vector2(1920, 1080), Vector2(3840, 2160) })
                for (unsigned fraction = 0; fraction < 1000; ++fraction)
                {
                    const float origin = static_cast<float>(fraction) / 1000;
                    const CameraViewport normalized{ origin, origin, 1.0f - origin, 1.0f - origin };
                    const Viewport pixels = CameraUtils::toPixelViewport(normalized, size);
                    require(pixels.x >= 0 && pixels.y >= 0 && pixels.width > 0 && pixels.height > 0
                        && pixels.x + pixels.width <= size.x && pixels.y + pixels.height <= size.y,
                        "Normalized viewport must remain inside the pixel target after float rounding.");
                    ++viewportChecks;
                }
            std::printf("PASS normalized viewport conversion: %zu edge and fractional cases.\n", viewportChecks);
        }

        void testSkinningBounds(const std::filesystem::path& shaderDirectory)
        {
            struct Vertex
            {
                Vector3 position, normal, tangent, bitangent;
                Vector4 color;
                Vector2 texCoord;
                std::array<std::uint32_t, 4> indices;
                Vector4 weights;
            };
            static_assert(sizeof(Vertex) == 104 && offsetof(Vertex, indices) == 72);
            std::mt19937 random(1729);
            std::uniform_real_distribution<float> coordinate(-5.0f, 5.0f);
            std::uniform_real_distribution<float> weight(0.0f, 1.0f);
            std::uniform_int_distribution<std::uint16_t> bone(0, 255);
            std::vector<ModelVertex> source(1024);
            std::vector<Vertex> vertices;
            for (std::size_t index = 0; index < source.size(); ++index)
            {
                ModelVertex& vertex = source[index];
                vertex.position = Vector3(coordinate(random), coordinate(random), coordinate(random));
                for (auto& influence : vertex.boneIndices)
                    influence = bone(random);
                vertex.boneWeights = Vector4(weight(random), weight(random), weight(random), weight(random));
                if (index % 17 == 0)
                    vertex.boneWeights = Vector4::Zero;
                if (index % 19 == 0)
                    vertex.boneWeights = Vector4(0.000005f, 0.000005f, 0, 0);
                if (index % 23 == 0)
                    vertex.boneWeights = Vector4(0.0000051f, 0.0000051f, 0, 0);
                if (index % 29 == 0)
                    vertex.boneIndices[0] = UINT16_MAX;
                if (index % 31 == 0)
                    vertex.boneWeights = Vector4(0.5f, 0.5f, 0.5f, 0.5f);
                vertices.push_back({ vertex.position, vertex.normal, vertex.tangent, vertex.bitangent,
                    vertex.color, vertex.texCoord, { vertex.boneIndices[0], vertex.boneIndices[1],
                    vertex.boneIndices[2], vertex.boneIndices[3] }, vertex.boneWeights });
            }
            SkinningBounds boundsCache;
            require(boundsCache.initialize(source), "Build influence bounds for randomized vertices.");
            SkinningPaletteSnapshot palette;
            palette.jointCount = MAX_SKINNING_BONES;
            palette.constants.boneNormalMatrices.fill(Matrix::Identity);
            for (Matrix& matrix : palette.constants.boneMatrices)
                matrix = Matrix::CreateScale(coordinate(random), coordinate(random), coordinate(random))
                    * Matrix::CreateRotationY(coordinate(random)) * Matrix::CreateRotationX(coordinate(random))
                    * Matrix::CreateTranslation(coordinate(random) * 10, coordinate(random) * 10, coordinate(random) * 10);

            DX12Shader shader;
            require(shader.load({ shaderDirectory / "skinning.cso" }), "Load actual Model.vsMain bounds test.");
            std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[0].Descriptor.ShaderRegister = 1;
            parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            parameters[1].Descriptor.ShaderRegister = 5;
            parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            const D3D12_DESCRIPTOR_RANGE range{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7, 0, 0 };
            parameters[2].DescriptorTable = { 1, &range };
            parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            parameters[3].Descriptor.ShaderRegister = 0;
            DX12ComputePipeline pipeline;
            require(pipeline.initialize(*m_device.Get(), shader, parameters), "Build skinning test compute pipeline.");
            DX12UploadBuffer vertexUpload, paletteUpload, objectUpload;
            require(vertexUpload.initialize(*m_device.Get(), m_fence, vertices.size() * sizeof(Vertex))
                && vertexUpload.write(std::as_bytes(std::span(vertices)))
                && paletteUpload.initialize(*m_device.Get(), m_fence, sizeof(SkinningPaletteConstants))
                && objectUpload.initialize(*m_device.Get(), m_fence, sizeof(Instance)), "Allocate skinning test uploads.");
            DX12DescriptorHeap heap;
            require(heap.initialize(*m_device.Get(), { .capacity = 1, .shaderVisible = true }), "Create vertex test SRV heap.");
            const auto descriptor = heap.allocate();
            require(descriptor && descriptor->gpu, "Allocate skinning test SRV.");
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.NumElements = static_cast<UINT>(vertices.size());
            srv.Buffer.StructureByteStride = sizeof(Vertex);
            m_device->CreateShaderResourceView(vertexUpload.get(), &srv, descriptor->cpu.native);
            DX12Resource output;
            D3D12_RESOURCE_DESC description{};
            description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            description.Width = vertices.size() * sizeof(Vector4);
            description.Height = 1;
            description.DepthOrArraySize = 1;
            description.MipLevels = 1;
            description.SampleDesc = { 1, 0 };
            description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            require(output.initialize(*m_device.Get(), { .description = description }), "Create GPU skin positions.");
            DX12ReadbackBuffer readback;
            require(readback.initialize(*m_device.Get(), m_fence, description.Width), "Create skin positions readback.");
            const std::array worlds{
                Matrix::Identity,
                Matrix::CreateScale(-1.5f, 0.25f, 3.0f) * Matrix::CreateRotationZ(0.8f)
                    * Matrix::CreateTranslation(100, -50, 70),
                Matrix::CreateTranslation(1000000, -1000000, 1000000),
                Matrix::CreateScale(0.0f, 1.0f, 0.0f) };
            for (std::size_t pose = 0; pose < worlds.size(); ++pose)
            {
                // Reuse the same influence cache while changing the current pose.
                for (Matrix& matrix : palette.constants.boneMatrices)
                    matrix *= Matrix::CreateTranslation(3.0f, -2.0f, 1.0f);
                AABB bounds;
                require(boundsCache.calculate(palette, worlds[pose], bounds), "Calculate current-pose world bounds.");
                const Instance instance{ worlds[pose], worlds[pose] };
                require(paletteUpload.write(std::as_bytes(std::span(&palette.constants, 1)))
                    && objectUpload.write(std::as_bytes(std::span(&instance, 1)))
                    && m_list.begin(m_fence) && pipeline.bind(m_list)
                    && output.transition(m_list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), "Record skinning containment test.");
                auto& native = *m_list.getForRecording();
                ID3D12DescriptorHeap* const nativeHeap = heap.get();
                native.SetDescriptorHeaps(1, &nativeHeap);
                native.SetComputeRootConstantBufferView(0, paletteUpload.getGpuVirtualAddress());
                native.SetComputeRootShaderResourceView(1, objectUpload.getGpuVirtualAddress());
                native.SetComputeRootDescriptorTable(2, descriptor->gpu->native);
                native.SetComputeRootUnorderedAccessView(3, output.get()->GetGPUVirtualAddress());
                native.Dispatch(static_cast<UINT>((vertices.size() + 63) / 64), 1, 1);
                require(output.transition(m_list, D3D12_RESOURCE_STATE_COPY_SOURCE), "Transition skinning output.");
                native.CopyBufferRegion(readback.get(), 0, output.get(), 0, description.Width);
                require(m_list.close(), "Close skinning test.");
                ID3D12CommandList* const list = m_list.getForExecution();
                m_queue->ExecuteCommandLists(1, &list);
                const auto fence = m_fence.signal(*m_queue.Get());
                require(fence != 0 && m_list.markSubmitted(fence) && vertexUpload.markUsed(fence)
                    && paletteUpload.markUsed(fence) && objectUpload.markUsed(fence)
                    && readback.markPending(fence) && m_fence.waitOnCpu(fence), "Submit skinning test.");
                std::vector<Vector4> positions(vertices.size());
                require(readback.read(std::as_writable_bytes(std::span(positions))), "Read production GPU skin positions.");
                for (const Vector4& position : positions)
                    require(std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)
                        && bounds.Contains(DirectX::XMVectorSet(position.x, position.y, position.z, 1)) != DirectX::DISJOINT,
                        "Bounds must contain every position produced by actual Model.vsMain.");
                checkDebugMessages();
            }
            std::puts("PASS actual Model.vsMain: 4096 GPU-skinned vertices inside current-pose bounds.");

            AABB sentinel(Vector3(1, 2, 3), Vector3(4, 5, 6));
            AABB result = sentinel;
            auto invalid = palette;
            invalid.constants.boneMatrices[255]._41 = std::numeric_limits<float>::quiet_NaN();
            require(!boundsCache.calculate(invalid, Matrix::Identity, result)
                && result.Center.x == sentinel.Center.x && result.Extents.x == sentinel.Extents.x, "Invalid palette must preserve output.");
            invalid = palette;
            invalid.constants.boneMatrices[255]._14 = 0.1f;
            require(!boundsCache.calculate(invalid, Matrix::Identity, result), "Projective bone must bypass culling.");
            invalid = palette;
            invalid.jointCount = 0;
            require(!boundsCache.calculate(invalid, Matrix::Identity, result), "Invalid joint count must bypass culling.");
            auto badVertex = source.front();
            badVertex.boneWeights.x = -1;
            SkinningBounds invalidCache;
            require(!invalidCache.initialize(std::span(&badVertex, 1)), "Negative weights must disable bounds.");
            badVertex.boneWeights = Vector4(std::numeric_limits<float>::infinity(), 0, 0, 0);
            require(!invalidCache.initialize(std::span(&badVertex, 1)), "Nonfinite weights must disable bounds.");
            require(!invalidCache.initialize({}), "Empty cache must fail.");
            require(!invalidCache.calculate(palette, Matrix::Identity, result), "Uninitialized cache must fail safely.");
            badVertex.boneWeights = Vector4(1, 0, 0, 0);
            badVertex.position.x = std::numeric_limits<float>::quiet_NaN();
            require(!invalidCache.initialize(std::span(&badVertex, 1)), "Nonfinite position must disable bounds.");
            badVertex.position = Vector3(1, 2, 3);
            require(invalidCache.initialize(std::span(&badVertex, 1))
                && invalidCache.calculate(palette, Matrix::Identity, result), "Cache can be rebuilt after failure.");
            Matrix unsafeWorld = Matrix::Identity;
            unsafeWorld._14 = 0.1f;
            require(!boundsCache.calculate(palette, unsafeWorld, result), "Projective world must bypass culling.");
            unsafeWorld = Matrix::CreateScale(FLT_MAX);
            require(!boundsCache.calculate(palette, unsafeWorld, result), "GPU arithmetic overflow risk must bypass culling.");

            const Matrix projection(DirectX::XMMatrixPerspectiveFovLH(1, 1, 0.1f, 100));
            const CameraFrustum frustum = CameraUtils::buildWorldFrustum(projection, Matrix::Identity);
            std::array<ModelVertex, 1> point{};
            point[0].boneWeights = Vector4(1, 0, 0, 0);
            SkinningBounds pointBounds;
            require(pointBounds.initialize(point), "Build single-bone frustum bounds.");
            palette.constants.boneMatrices.fill(Matrix::Identity);
            palette.constants.boneMatrices[0] = Matrix::CreateTranslation(100, 0, 5);
            require(pointBounds.calculate(palette, Matrix::Identity, result) && !isVisible(frustum, result), "Animated offscreen bounds.");
            RenderItem item;
            item.vertexBuffer = &m_vertexView;
            item.indexBuffer = &m_indexView;
            item.indexCount = 3;
            item.worldBounds = result;
            item.skinningPalette = std::make_shared<SkinningPaletteSnapshot>(palette);
            RenderQueue queue;
            require(queue.submit(item, &frustum), "Unvalidated animation must remain submitted.");
            item.animationBoundsValid = true;
            require(!queue.submit(item, &frustum), "Validated offscreen animation must be frustum-culled.");
            palette.constants.boneMatrices[0] = Matrix::CreateTranslation(0, 0, 5);
            require(pointBounds.calculate(palette, Matrix::Identity, result) && isVisible(frustum, result), "Moving back into view uses current pose.");
            item.worldBounds = result;
            require(queue.submit(item, &frustum), "Animated mesh returning to view must be submitted.");
            std::puts("PASS animated frustum and invalid-input safety.");
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
        DX12GraphicsPipeline m_graphics, m_equalGraphics;
        std::array<DX12GpuOcclusionCulling, 2> m_frames;
        DX12DescriptorHeap m_targets, m_depthTargets;
        D3D12_CPU_DESCRIPTOR_HANDLE m_rtv{}, m_dsv{};
        DX12Resource m_depth, m_color, m_recordedInstances;
        DX12UploadBuffer m_depthUpload, m_instances, m_indices;
        DX12ReadbackBuffer m_directImage, m_indirectImage, m_pipelineResults, m_recordedReadback;
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
        tests.testCameraCulling();
        tests.testSkinningBounds(argv[1]);
        tests.run("fully hidden: zero Draw", { {} }, 1);
        tests.run("visible", { { Vector3(0, 0, 0.2f) } }, 0);
        tests.run("mixed batch: compact visible instance", { {}, { Vector3(0.3f, 0, 0.2f) } }, 1);
        tests.run("instancing disabled", { {}, { Vector3(-0.3f, 0, 0.2f) } }, 1, false);
        tests.run("near plane crossing", { { Vector3(0, 0, 0.01f) } }, 0);
        tests.run("unvalidated skinning stays visible", { { Vector3(0, 0, 0.8f), Vector3(0.15f), true } }, 0);
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
        const CameraViewport edge{ 0.001f, 0.001f, 0.999f, 0.999f };
        const Viewport pixels = CameraUtils::toPixelViewport(edge, Vector2(1920, 1080));
        tests.run("normalized viewport right/bottom edge", { {} }, 1, true, 1920, 1080, Matrix::Identity,
            D3D12_VIEWPORT{ pixels.x, pixels.y, pixels.width, pixels.height, 0, 1 });
        tests.run("pixel viewport right edge float overshoot", { {} }, 1, true, 1920, 64, Matrix::Identity,
            D3D12_VIEWPORT{ edge.x * 1920, 0, edge.width * 1920, 64, 0, 1 });
        tests.run("pixel viewport bottom edge float overshoot", { {} }, 1, true, 64, 1920, Matrix::Identity,
            D3D12_VIEWPORT{ 0, edge.y * 1920, 64, edge.height * 1920, 0, 1 });
        tests.run("one-pixel target", { {} }, 1, true, 1, 1);
        tests.run("empty queue", {}, 0);
        std::vector<TestObject> largeBatch(257);
        for (std::size_t index = 0; index < largeBatch.size(); index += 3)
            largeBatch[index].center.z = 0.2f;
        tests.run("multi-wave compaction", largeBatch, 171);
        for (const std::size_t count : { 63u, 64u, 65u, 127u, 128u, 129u, 257u, 1025u })
        {
            std::vector<TestObject> coplanar(count);
            for (std::size_t index = 0; index < count; ++index)
                coplanar[index].center = Vector3(-0.02f + 0.04f * static_cast<float>(index) / static_cast<float>(count - 1), 0, 0.2f);
            const std::string name = "stable coplanar compaction " + std::to_string(count);
            tests.run(name.c_str(), coplanar, 0, true, 64, 64, Matrix::Identity, std::nullopt, std::nullopt, 0.2f, true);
            std::uint32_t hidden = 0;
            for (std::size_t index = 0; index < count; ++index)
                if (index % 3 == 0)
                {
                    coplanar[index].center.z = 0.8f;
                    ++hidden;
                }
            const std::string mixedName = "stable filtered coplanar compaction " + std::to_string(count);
            tests.run(mixedName.c_str(), coplanar, hidden, true, 64, 64, Matrix::Identity, std::nullopt, std::nullopt, 0.2f, true);
        }
        std::vector<TestObject> emptyTiles(257);
        emptyTiles.front().center = Vector3(-0.02f, 0, 0.2f);
        emptyTiles.back().center = Vector3(0.02f, 0, 0.2f);
        tests.run("stable compaction across fully hidden tiles", emptyTiles, 255, true, 64, 64,
            Matrix::Identity, std::nullopt, std::nullopt, 0.2f, true);
        tests.run("multi-tile batch fully hidden", std::vector<TestObject>(129), 129, true, 64, 64,
            Matrix::Identity, std::nullopt, std::nullopt, 0.2f, true);
        tests.run("camera movement has no stale results", { {} }, 0, true, 64, 64, Matrix::CreateTranslation(0, 0, -0.6f));
        const Matrix perspective(DirectX::XMMatrixPerspectiveFovLH(1.0f, 1.0f, 0.1f, 100.0f));
        tests.run("perspective depth", { { Vector3(0, 0, 5) } }, 1, true, 64, 64, perspective);
        tests.run("camera inside bounds", { { Vector3(0, 0, 0.1f), Vector3(1) } }, 0, true, 64, 64, perspective);
        tests.run("signed base vertex and index offsets", {
            { Vector3(-0.3f, 0, 0.2f), Vector3(0.15f), false, RenderPassType::Opaque, 0, -1 },
            { Vector3(0.3f, 0, 0.2f) }, {} }, 1);
        tests.run("dispatch chunk boundary", std::vector<TestObject>(65536), 65536, false, 1, 1);
        auto animated = std::make_shared<SkinningPaletteSnapshot>();
        animated->jointCount = 1;
        animated->constants.boneMatrices.fill(Matrix::Identity);
        animated->constants.boneNormalMatrices.fill(Matrix::Identity);
        TestObject animatedObject;
        animatedObject.palette = animated;
        tests.run("animated hidden bounds suppress Draw", { animatedObject }, 1);
        animated->constants.boneMatrices[0] = Matrix::CreateTranslation(0, 0, -0.6f);
        tests.run("animated pose returns in front of wall", { animatedObject }, 0);
        animated->constants.boneMatrices[0] = Matrix::CreateTranslation(0, 0, -0.8f);
        tests.run("animated near-plane bounds stay visible", { animatedObject }, 0);
        animated->constants.boneMatrices[0] = Matrix::Identity;
        animatedObject.pass = RenderPassType::Transparent;
        tests.run("animated transparent draw remains direct", { animatedObject }, 0);
        animatedObject.pass = RenderPassType::Shadow;
        tests.run("animated shadow draw remains direct", { animatedObject }, 0);
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
