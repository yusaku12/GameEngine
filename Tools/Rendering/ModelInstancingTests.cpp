#include "Pch.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Model\ModelManager.h"
#include "Graphics\DirectX12\Renderer.h"
#include <d3d12sdklayers.h>
#include <imgui.h>

namespace
{
    using namespace Engine;

    int failures = 0;

    void check(const bool condition, const char* const name)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", name);
            ++failures;
        }
    }

    Microsoft::WRL::ComPtr<ID3D12InfoQueue> getDebugQueue()
    {
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
            return {};
        for (UINT index = 0;; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
                return {};
            if (adapter == nullptr)
                return {};
            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(adapter->GetDesc1(&description)))
                return {};
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                continue;
            Microsoft::WRL::ComPtr<ID3D12Device> device;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))))
            {
                Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
                if (FAILED(device.As(&queue)))
                    return {};
                return queue;
            }
        }
    }

    void checkDebugMessages(ID3D12InfoQueue& queue)
    {
        const UINT64 count = queue.GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 index = 0; index < count; ++index)
        {
            SIZE_T size = 0;
            if (FAILED(queue.GetMessage(index, nullptr, &size)))
            {
                check(false, "Read GPU debug message size");
                continue;
            }
            std::vector<std::byte> bytes(size);
            auto* const message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            if (FAILED(queue.GetMessage(index, message, &size)))
            {
                check(false, "Read GPU debug message");
                continue;
            }
            if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION
                || message->Severity == D3D12_MESSAGE_SEVERITY_ERROR
                || message->Severity == D3D12_MESSAGE_SEVERITY_WARNING)
            {
                std::fprintf(stderr, "GPU validation: %s\n", message->pDescription);
                check(false, "GPU debug layer has no warnings or errors");
            }
        }
    }

    void drawInstances(DX12Renderer& renderer, const ModelHandle model, const MaterialHandle material,
        const std::uint32_t count, const bool propertyOverride, const bool animated,
        const std::uint32_t expectedDraws, const std::uint32_t expectedInstances)
    {
        auto palette = std::make_shared<SkinningPaletteSnapshot>();
        palette->jointCount = 1;
        palette->constants.boneMatrices.fill(Matrix::Identity);
        palette->constants.boneNormalMatrices.fill(Matrix::Identity);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            ModelRenderSubmission submission;
            submission.model = model;
            submission.materials = { material };
            submission.objectID = index;
            submission.worldMatrix = Matrix::CreateTranslation(
                static_cast<float>(index % 10) * 0.15f - 0.75f,
                static_cast<float>(index / 10 % 10) * 0.15f - 0.75f, 0.5f);
            if (propertyOverride)
                check(submission.materialProperties.setFloat(MaterialParameters::Metallic, 0.5f), "Set override");
            if (animated)
                submission.skinningPalette = palette;
            ModelRenderSubmissionQueue::instance().submit(std::move(submission));
        }
        if (!renderer.render())
        {
            check(false, "Renderer frame succeeds");
            return;
        }
        const RendererStatistics& statistics = renderer.getStatistics();
        std::printf("Objects=%u Draws=%u Instances=%u ExpectedDraws=%u\n",
            count, statistics.drawCallCount, statistics.instanceCount, expectedDraws);
        check(statistics.drawCallCount == expectedDraws, "Exact draw count");
        check(statistics.instanceCount == expectedInstances, "All pass instances retained");
        check(statistics.batchCount == expectedDraws, "Batch count matches GPU draws");
        check(statistics.visibleObjects == count, "All submitted objects visible");
    }
}

int main()
{
    using namespace Engine;
    if (!Logger::instance().initialize())
        return 1;
    const HWND window = CreateWindowExW(0, L"STATIC", L"GameEngine Instancing Test",
        WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr)
    {
        std::fprintf(stderr, "Could not create test window.\n");
        Logger::instance().finalize();
        return 1;
    }

    {
        DX12Renderer renderer;
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> debugQueue;
        if (!renderer.initialize(window, 640, 480))
        {
            check(false, "Renderer initializes");
        }
        else
        {
            ImGui::GetIO().IniFilename = nullptr;
            debugQueue = getDebugQueue();
            check(debugQueue != nullptr, "GPU validation queue available");
            ModelResource resource;
            MeshResource mesh;
            mesh.vertices.resize(3);
            mesh.vertices[0].position = Vector3(-0.05f, -0.05f, 0.0f);
            mesh.vertices[1].position = Vector3(0.0f, 0.05f, 0.0f);
            mesh.vertices[2].position = Vector3(0.05f, -0.05f, 0.0f);
            for (ModelVertex& vertex : mesh.vertices)
                vertex.boneWeights = Vector4(1.0f, 0.0f, 0.0f, 0.0f);
            mesh.indices = { 0, 1, 2 };
            resource.meshes.push_back(std::move(mesh));
            const ModelHandle model = ModelManager::instance().create(std::move(resource));
            check(model.isValid(), "Test mesh registered");
            const MaterialHandle opaque = MaterialManager::instance().getDefaultMaterial();
            MaterialAsset alphaAsset;
            alphaAsset.renderState.surfaceType = MaterialSurfaceType::AlphaTest;
            const MaterialHandle alpha = MaterialManager::instance().create(std::move(alphaAsset));
            MaterialAsset transparentAsset;
            transparentAsset.renderState.surfaceType = MaterialSurfaceType::Transparent;
            const MaterialHandle transparent = MaterialManager::instance().create(std::move(transparentAsset));

            renderer.setModelInstancingEnabled(true);
            drawInstances(renderer, model, opaque, 100, false, false, 2, 200);
            renderer.setModelInstancingEnabled(false);
            drawInstances(renderer, model, opaque, 100, false, false, 200, 200);
            renderer.setModelInstancingEnabled(true);
            drawInstances(renderer, model, opaque, 600, false, false, 2, 1200);
            drawInstances(renderer, model, opaque, 600, false, false, 2, 1200);
            drawInstances(renderer, model, opaque, 1, false, false, 2, 2);
            drawInstances(renderer, model, opaque, 0, false, false, 0, 0);
            drawInstances(renderer, model, opaque, 10, true, false, 20, 20);
            drawInstances(renderer, model, opaque, 10, false, true, 20, 20);
            drawInstances(renderer, model, transparent, 10, false, false, 10, 10);
            renderer.setShadowViewProjection(Matrix::Identity);
            drawInstances(renderer, model, alpha, 100, false, false, 3, 300);
            drawInstances(renderer, model, opaque, 100, false, false, 3, 300);
            // Reusing both frame slots waits for all model draws before reading GPU validation messages.
            drawInstances(renderer, model, opaque, 0, false, false, 0, 0);
            drawInstances(renderer, model, opaque, 0, false, false, 0, 0);
        }
        if (debugQueue != nullptr)
            checkDebugMessages(*debugQueue.Get());
        debugQueue.Reset();
        SceneManager::instance().shutdown();
        check(renderer.finalize(), "Renderer finalizes after GPU work");
    }
    ModelManager::instance().clear();
    MaterialManager::instance().clear();
    DestroyWindow(window);
    Logger::instance().finalize();
    std::printf("Model instancing integration tests: %s (%d failures)\n",
        failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
