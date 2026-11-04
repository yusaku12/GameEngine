#include "Pch.h"
#include "Validation\Rendering\RenderingValidation.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Model\ModelManager.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\DirectX12\Diagnostics\FrameCapture.h"
#include "Graphics\DirectX12\Rendering\Renderer.h"
#include "Validation\Rendering\PostProcessAuthoringValidation.h"
#include "Validation\Rendering\PostProcessEffectsValidation.h"
#include "Validation\Rendering\PostProcessVolumeValidation.h"
#include "Validation\Rendering\PostProcessCustomEffectValidation.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Graphics\Texture\TextureManager.h"
#include <cmath>
#include <cstring>
#include <DirectXPackedVector.h>

namespace Engine
{
    namespace
    {
        constexpr std::uint32_t BASE_WIDTH = 1280;
        constexpr std::uint32_t BASE_HEIGHT = 720;
        constexpr float HDR_TOLERANCE = 0.003f;
        constexpr unsigned int DISPLAY_TOLERANCE = 2;

        struct ComApartment
        {
            HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~ComApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
        };

        struct CustomEffectFixture
        {
            PostProcessEffectHandle handle;
            ~CustomEffectFixture()
            {
                if (handle.isValid() && PostProcessEffectAssetManager::instance().get(handle))
                    PostProcessEffectAssetManager::instance().unload(handle);
            }
        };

        struct Probe
        {
            const char* name;
            float u;
            Vector3 expected;
        };

        const std::array PROBES = {
            Probe{ "middle-gray", 0.12f, Vector3(0.18f, 0.18f, 0.18f) },
            Probe{ "hdr-highlight", 0.27f, Vector3(4.0f, 2.0f, 1.0f) },
            Probe{ "linear-albedo", 0.42f, Vector3(0.03f, 0.06f, 0.09f) },
            Probe{ "transparent-over-opaque", 0.57f, Vector3(1.1f, 0.45f, 0.425f) },
            Probe{ "alpha-test-discard", 0.72f, Vector3(0.08f, 0.16f, 0.24f) },
            Probe{ "alpha-test-visible", 0.87f, Vector3(0.5f, 0.25f, 1.0f) },
        };

        struct ValidationScene
        {
            ModelHandle model;
            std::vector<MaterialHandle> materials;
            std::vector<ModelRenderSubmission> draws;
            ModelRenderSubmission bloomHotspot;

            ~ValidationScene()
            {
                ModelRenderSubmissionQueue::instance().clear();
                CameraRenderSubmissionQueue::instance().clear();
                for (const MaterialHandle material : materials)
                    MaterialManager::instance().unload(material);
                if (model.isValid())
                    ModelManager::instance().unload(model);
            }

            bool initialize(const std::filesystem::path& whitePath)
            {
                ModelResource resource;
                MeshResource mesh;
                mesh.name = "Phase0Quad";
                constexpr std::array positions = {
                    Vector3(-0.4f, -0.6f, 0.0f), Vector3(-0.4f, 0.6f, 0.0f),
                    Vector3(0.4f, 0.6f, 0.0f), Vector3(0.4f, -0.6f, 0.0f),
                };
                for (const Vector3& position : positions)
                {
                    ModelVertex vertex;
                    vertex.position = position;
                    vertex.normal = Vector3(0.0f, 0.0f, -1.0f);
                    vertex.bitangent = Vector3(0.0f, -1.0f, 0.0f);
                    mesh.vertices.push_back(vertex);
                }
                mesh.indices = { 0, 1, 2, 0, 2, 3 };
                mesh.subMeshes.push_back({ .indexCount = 6 });
                mesh.boundingBox = makeAABB(positions.data(), positions.size());
                mesh.boundingSphere = BoundingSphere(Vector3::Zero, 0.8f);
                resource.boundingBox = mesh.boundingBox;
                resource.boundingSphere = mesh.boundingSphere;
                resource.meshes.push_back(std::move(mesh));
                model = ModelManager::instance().create(std::move(resource));
                if (!model.isValid())
                {
                    LOG_ERROR("[Validation] Cannot create procedural geometry.");
                    return false;
                }
                const AssetGUID whiteGuid = TextureManager::instance().registerAssetPath(whitePath);
                if (!whiteGuid.isValid())
                {
                    LOG_ERROR("[Validation] Cannot register generated white texture.");
                    return false;
                }
                const auto add = [this, &whitePath, &whiteGuid](const char* name, const float u,
                    const float z, const Vector3& baseColor, const Vector3& emission,
                    const float alpha, const MaterialSurfaceType surface)
                {
                    MaterialAsset asset;
                    asset.name = name;
                    asset.baseColor = Vector4(baseColor.x, baseColor.y, baseColor.z, alpha);
                    asset.emissiveColor = emission;
                    asset.emissiveIntensity = 1.0f;
                    asset.textures.emissive = whiteGuid;
                    asset.textures.emissivePath = whitePath;
                    asset.renderState.surfaceType = surface;
                    asset.renderState.cullMode = MaterialCullMode::None;
                    if (surface == MaterialSurfaceType::Transparent)
                    {
                        asset.renderState.depthWrite = false;
                        asset.renderState.blendMode = MaterialBlendMode::Alpha;
                    }
                    const MaterialHandle material = MaterialManager::instance().create(std::move(asset));
                    if (!material.isValid())
                    {
                        LOG_ERROR("[Validation] Cannot create material '{}'.", name);
                        return false;
                    }
                    materials.push_back(material);
                    ModelRenderSubmission draw;
                    draw.model = model;
                    draw.materials.push_back(material);
                    draw.worldMatrix = Matrix::CreateTranslation((u - 0.5f) * 8.0f, 0.0f, z);
                    ModelManager::instance().get(model)->boundingBox.Transform(draw.worldBounds, draw.worldMatrix);
                    draw.objectID = static_cast<std::uint32_t>(draws.size() + 1);
                    draw.castShadows = false;
                    draws.push_back(std::move(draw));
                    return true;
                };
                return add("MiddleGray", 0.12f, 2.0f, Vector3::Zero,
                        PROBES[0].expected, 1.0f, MaterialSurfaceType::Opaque)
                    && add("Highlight", 0.27f, 2.0f, Vector3::Zero,
                        PROBES[1].expected, 1.0f, MaterialSurfaceType::Opaque)
                    && add("LinearAlbedo", 0.42f, 2.0f, Vector3(0.2f, 0.4f, 0.6f),
                        Vector3::Zero, 1.0f, MaterialSurfaceType::Opaque)
                    && add("TransparencyBackground", 0.57f, 3.0f, Vector3::Zero,
                        Vector3(0.2f, 0.4f, 0.6f), 1.0f, MaterialSurfaceType::Opaque)
                    && add("TransparencyForeground", 0.57f, 2.0f, Vector3::Zero,
                        Vector3(2.0f, 0.5f, 0.25f), 0.5f, MaterialSurfaceType::Transparent)
                    && add("Discarded", 0.72f, 2.0f, Vector3::Zero,
                        Vector3(8.0f, 0.0f, 0.0f), 0.25f, MaterialSurfaceType::AlphaTest)
                    && add("VisibleAlphaTest", 0.87f, 2.0f, Vector3::Zero,
                        PROBES[5].expected, 0.75f, MaterialSurfaceType::AlphaTest)
                    && initializeBloomHotspot(whiteGuid, whitePath);
            }

            bool initializeBloomHotspot(const AssetGUID whiteGuid, const std::filesystem::path& whitePath)
            {
                MaterialAsset asset;
                asset.name = "BloomHotspot";
                asset.emissiveColor = Vector3(64.0f, 48.0f, 32.0f);
                asset.emissiveIntensity = 1.0f;
                asset.textures.emissive = whiteGuid;
                asset.textures.emissivePath = whitePath;
                asset.renderState.cullMode = MaterialCullMode::None;
                const MaterialHandle material = MaterialManager::instance().create(std::move(asset));
                if (!material.isValid())
                {
                    LOG_ERROR("[Validation] Cannot create the Bloom hotspot material.");
                    return false;
                }
                materials.push_back(material);
                bloomHotspot.model = model;
                bloomHotspot.materials.push_back(material);
                bloomHotspot.worldMatrix = Matrix::CreateScale(0.025f, 0.025f, 1.0f)
                    * Matrix::CreateTranslation(0.01f, 0.01f, 2.0f);
                ModelManager::instance().get(model)->boundingBox.Transform(
                    bloomHotspot.worldBounds, bloomHotspot.worldMatrix);
                bloomHotspot.objectID = 100;
                bloomHotspot.castShadows = false;
                return true;
            }

            bool render(DX12Renderer& renderer, const std::uint32_t width, const std::uint32_t height,
                const CameraViewport& viewport = {}, const CameraClearMode clearMode = CameraClearMode::SolidColor,
                const PostProcessCameraSnapshot& postProcess = {},
                const PostProcessValidationFixture fixture = PostProcessValidationFixture::Panels) const
            {
                RenderView view;
                view.camera.projection = Matrix(DirectX::XMMatrixOrthographicLH(8.0f, 4.0f, 0.1f, 10.0f));
                if (fixture == PostProcessValidationFixture::Diagonal)
                    view.camera.view = Matrix::CreateRotationZ(0.27f);
                view.camera.viewProjection = view.camera.view * view.camera.projection;
                view.camera.previousViewProjection = view.camera.viewProjection;
                view.camera.inverseProjection = view.camera.projection.Invert();
                view.camera.inverseViewProjection = view.camera.viewProjection.Invert();
                view.camera.viewportSize = Vector2(static_cast<float>(width), static_cast<float>(height));
                view.camera.inverseViewportSize = Vector2(1.0f / width, 1.0f / height);
                view.camera.nearClip = 0.1f;
                view.camera.farClip = 10.0f;
                view.viewport = viewport;
                view.clearMode = clearMode;
                view.postProcess = postProcess;
                if (fixture != PostProcessValidationFixture::NoCamera)
                    CameraRenderSubmissionQueue::instance().submit(view);
                if (fixture == PostProcessValidationFixture::BloomHotspot)
                    ModelRenderSubmissionQueue::instance().submit(bloomHotspot);
                else if (fixture == PostProcessValidationFixture::Panels || fixture == PostProcessValidationFixture::Diagonal
                    || fixture == PostProcessValidationFixture::Nothing)
                    for (const ModelRenderSubmission& draw : draws)
                        ModelRenderSubmissionQueue::instance().submit(draw);
                FrameProfiler& profiler = FrameProfiler::instance();
                const std::uint64_t frameId = profiler.beginFrame();
                if (frameId == 0)
                    return false;
                const auto start = std::chrono::steady_clock::now();
                const bool rendered = renderer.render();
                const double duration = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
                const bool measured = profiler.endFrame(frameId, duration);
                return rendered && measured;
            }
        };

        float displayReference(const float hdr, const float hdrScale = 1.0f, const float ldrScale = 1.0f)
        {
            const double value = std::max(static_cast<double>(hdr) * hdrScale, 0.0);
            const double mapped = std::clamp(std::clamp(
                value * (2.51 * value + 0.03) / (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0) * ldrScale, 0.0, 1.0);
            return static_cast<float>(mapped <= 0.0031308
                ? mapped * 12.92 : 1.055 * std::pow(mapped, 1.0 / 2.4) - 0.055);
        }

        bool identical(const DirectX::ScratchImage& left, const DirectX::ScratchImage& right)
        {
            const DirectX::Image* const a = left.GetImage(0, 0, 0);
            const DirectX::Image* const b = right.GetImage(0, 0, 0);
            if (a == nullptr || b == nullptr || a->width != b->width || a->height != b->height
                || a->format != b->format)
                return false;
            const std::size_t rowSize = a->width * DirectX::BitsPerPixel(a->format) / 8;
            for (std::size_t y = 0; y < a->height; ++y)
                if (std::memcmp(a->pixels + y * a->rowPitch, b->pixels + y * b->rowPitch, rowSize) != 0)
                    return false;
            return true;
        }

        bool verifySavedCapture(const DX12FrameCapture& capture,
            const std::filesystem::path& directory, const std::wstring& name)
        {
            DirectX::ScratchImage hdr;
            DirectX::ScratchImage display;
            HRESULT result = DirectX::LoadFromDDSFile((directory / (name + L"-hdr.dds")).c_str(),
                DirectX::DDS_FLAGS_NONE, nullptr, hdr);
            if (SUCCEEDED(result))
                result = DirectX::LoadFromWICFile((directory / (name + L"-display.png")).c_str(),
                    DirectX::WIC_FLAGS_IGNORE_SRGB | DirectX::WIC_FLAGS_FORCE_RGB, nullptr, display);
            if (FAILED(result))
            {
                LOG_ERROR("[Validation] Saved image readback failed (HRESULT: 0x{:08X}).",
                    static_cast<unsigned long>(result));
                return false;
            }
            if (!identical(capture.hdr, hdr) || !identical(capture.display, display))
            {
                LOG_ERROR("[Validation] Saved images do not match GPU readback.");
                return false;
            }
            return true;
        }

        struct CaseResult
        {
            const char* name;
            std::uint32_t width;
            std::uint32_t height;
            float peakHdr = 0.0f;
            unsigned int maxDisplayError = 0;
            float hdrScale = 1.0f;
            float ldrScale = 1.0f;
            std::uint32_t resolutionDivisor = 1;
            bool exactBaseline = true;
            std::uint32_t expectedEffects = 0;
            std::uint32_t expectedDraws = 1;
            std::size_t temporaryTargets = 0;
        };

        template<class Load>
        std::array<float, 3> sampleLinear(const float x, const float y,
            const std::size_t width, const std::size_t height, const Load& load)
        {
            const float px = std::clamp(x, 0.0f, static_cast<float>(width - 1));
            const float py = std::clamp(y, 0.0f, static_cast<float>(height - 1));
            const auto left = static_cast<std::size_t>(px);
            const auto top = static_cast<std::size_t>(py);
            const float fx = px - static_cast<float>(left);
            const float fy = py - static_cast<float>(top);
            const auto a = load(left, top);
            const auto b = load(std::min(left + 1, width - 1), top);
            const auto c = load(left, std::min(top + 1, height - 1));
            const auto d = load(std::min(left + 1, width - 1), std::min(top + 1, height - 1));
            std::array<float, 3> value{};
            for (std::size_t channel = 0; channel < value.size(); ++channel)
                value[channel] = std::lerp(std::lerp(a[channel], b[channel], fx),
                    std::lerp(c[channel], d[channel], fx), fy);
            return value;
        }

        bool validateCapture(const DX12FrameCapture& capture, CaseResult& result)
        {
            const DirectX::Image* const hdr = capture.hdr.GetImage(0, 0, 0);
            const DirectX::Image* const display = capture.display.GetImage(0, 0, 0);
            if (hdr == nullptr || display == nullptr || hdr->width != result.width
                || hdr->height != result.height || display->width != result.width
                || display->height != result.height || hdr->format != DXGI_FORMAT_R16G16B16A16_FLOAT
                || display->format != DXGI_FORMAT_R8G8B8A8_UNORM)
            {
                LOG_ERROR("[Validation] '{}' has unexpected image dimensions or formats.", result.name);
                return false;
            }
            DirectX::ScratchImage linear;
            const HRESULT converted = DirectX::Convert(*hdr, DXGI_FORMAT_R32G32B32A32_FLOAT,
                DirectX::TEX_FILTER_DEFAULT, 0.0f, linear);
            if (FAILED(converted))
            {
                LOG_ERROR("[Validation] HDR conversion failed (HRESULT: 0x{:08X}).",
                    static_cast<unsigned long>(converted));
                return false;
            }
            const DirectX::Image& image = *linear.GetImage(0, 0, 0);
            const auto lowWidth = (image.width + result.resolutionDivisor - 1) / result.resolutionDivisor;
            const auto lowHeight = (image.height + result.resolutionDivisor - 1) / result.resolutionDivisor;
            std::vector<std::array<float, 3>> lowResolution;
            if (result.resolutionDivisor != 1)
            {
                lowResolution.resize(lowWidth * lowHeight);
                const auto load = [&image](const std::size_t x, const std::size_t y)
                {
                    std::array<float, 3> rgb{};
                    std::memcpy(rgb.data(), image.pixels + y * image.rowPitch + x * sizeof(float) * 4, sizeof(rgb));
                    return rgb;
                };
                for (std::size_t y = 0; y < lowHeight; ++y)
                    for (std::size_t x = 0; x < lowWidth; ++x)
                    {
                        auto rgb = sampleLinear((static_cast<float>(x) + 0.5f) * image.width / lowWidth - 0.5f,
                            (static_cast<float>(y) + 0.5f) * image.height / lowHeight - 0.5f,
                            image.width, image.height, load);
                        for (float& value : rgb)
                            value = DirectX::PackedVector::XMConvertHalfToFloat(
                                DirectX::PackedVector::XMConvertFloatToHalf(value));
                        lowResolution[y * lowWidth + x] = rgb;
                    }
            }
            for (std::size_t y = 0; y < image.height; ++y)
            {
                for (std::size_t x = 0; x < image.width; ++x)
                {
                    std::array<float, 4> rgba{};
                    std::memcpy(rgba.data(), image.pixels + y * image.rowPitch + x * sizeof(rgba), sizeof(rgba));
                    const std::uint8_t* const pixel = display->pixels + y * display->rowPitch + x * 4;
                    std::array<float, 3> displayInput{ rgba[0], rgba[1], rgba[2] };
                    if (!lowResolution.empty())
                        displayInput = sampleLinear((static_cast<float>(x) + 0.5f) * lowWidth / image.width - 0.5f,
                            (static_cast<float>(y) + 0.5f) * lowHeight / image.height - 0.5f, lowWidth, lowHeight,
                            [&lowResolution, lowWidth](const std::size_t sx, const std::size_t sy)
                            { return lowResolution[sy * lowWidth + sx]; });
                    for (std::size_t channel = 0; channel < rgba.size(); ++channel)
                    {
                        if (!std::isfinite(rgba[channel]) || rgba[channel] < 0.0f)
                        {
                            LOG_ERROR("[Validation] '{}' has invalid HDR at ({}, {}).", result.name, x, y);
                            return false;
                        }
                        if (channel < 3)
                        {
                            result.peakHdr = std::max(result.peakHdr, rgba[channel]);
                            const int expected = static_cast<int>(std::lround(displayReference(displayInput[channel],
                                result.hdrScale, result.ldrScale) * 255.0f));
                            const auto difference = static_cast<unsigned int>(
                                std::abs(static_cast<int>(pixel[channel]) - expected));
                            result.maxDisplayError = std::max(result.maxDisplayError, difference);
                        }
                    }
                    if (pixel[3] != 255)
                    {
                        LOG_ERROR("[Validation] '{}' has unexpected final alpha.", result.name);
                        return false;
                    }
                }
            }
            if (result.maxDisplayError > DISPLAY_TOLERANCE)
            {
                LOG_ERROR("[Validation] '{}' tone-map/sRGB error {} exceeds {} byte levels.",
                    result.name, result.maxDisplayError, DISPLAY_TOLERANCE);
                return false;
            }
            for (const Probe& probe : PROBES)
            {
                const std::size_t centerX = static_cast<std::size_t>(probe.u * result.width);
                const std::size_t centerY = result.height / 2;
                const std::array expected = { probe.expected.x, probe.expected.y, probe.expected.z };
                for (std::size_t y = centerY - 2; y <= centerY + 2; ++y)
                {
                    for (std::size_t x = centerX - 2; x <= centerX + 2; ++x)
                    {
                        std::array<float, 4> pixel{};
                        std::memcpy(pixel.data(), image.pixels + y * image.rowPitch + x * sizeof(pixel), sizeof(pixel));
                        for (std::size_t channel = 0; channel < expected.size(); ++channel)
                        {
                            if (std::abs(pixel[channel] - expected[channel]) > HDR_TOLERANCE)
                            {
                                LOG_ERROR("[Validation] '{}': probe '{}' channel {} expected {}, got {}.",
                                    result.name, probe.name, channel, expected[channel], pixel[channel]);
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        }

        bool runValidation(DX12Renderer& renderer, const std::filesystem::path& requestedDirectory)
        {
            ComApartment apartment;
            if (FAILED(apartment.status))
            {
                LOG_ERROR("[Validation] COM initialization failed (HRESULT: 0x{:08X}).",
                    static_cast<unsigned long>(apartment.status));
                return false;
            }
            if (!renderer.validateDebugMessages())
                return false;
            std::error_code error;
            const std::filesystem::path directory = std::filesystem::absolute(requestedDirectory, error);
            if (error)
            {
                LOG_ERROR("[Validation] Invalid output directory: {}.", error.message());
                return false;
            }
            const bool exists = std::filesystem::exists(directory, error);
            if (error)
            {
                LOG_ERROR("[Validation] Cannot inspect output directory: {}.", error.message());
                return false;
            }
            if (exists)
            {
                const bool isDirectory = std::filesystem::is_directory(directory, error);
                if (error || !isDirectory)
                {
                    LOG_ERROR("[Validation] Output path is not a directory: '{}'.", directory.string());
                    return false;
                }
                const bool empty = std::filesystem::is_empty(directory, error);
                if (error || !empty)
                {
                    LOG_ERROR("[Validation] Output directory must be empty: '{}'.", directory.string());
                    return false;
                }
            }
            else if (!std::filesystem::create_directories(directory, error) || error)
            {
                LOG_ERROR("[Validation] Cannot create output directory: {}.", error.message());
                return false;
            }

            PostProcessCameraSnapshot authoringSnapshot;
            if (!runPostProcessAuthoringValidation(directory, authoringSnapshot))
                return false;
            DirectX::ScratchImage white;
            HRESULT result = white.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1);
            if (SUCCEEDED(result))
            {
                const DirectX::Image* const image = white.GetImage(0, 0, 0);
                std::memset(image->pixels, 255, 4);
                result = DirectX::SaveToDDSFile(*image, DirectX::DDS_FLAGS_NONE, (directory / L"white.dds").c_str());
            }
            if (FAILED(result))
            {
                LOG_ERROR("[Validation] White fixture texture creation failed (HRESULT: 0x{:08X}).",
                    static_cast<unsigned long>(result));
                return false;
            }

            ValidationScene scene;
            if (!scene.initialize(directory / L"white.dds"))
                return false;
            renderer.setEditorOverlayEnabled(false);
            auto scaleShader = std::make_shared<DX12Shader>();
            if (!scaleShader->load({ .bytecodePath = L"Assets\\Shaders\\Compiled\\PostProcessValidation_psScale_ps.cso" }))
                return false;
            const auto scalePass = [&scaleShader](const PostProcessStage stage, const float scale,
                const std::uint32_t divisor = 1, const bool enabled = true)
            {
                PostProcessPassDescription pass;
                pass.name = "ValidationScale";
                pass.stage = stage;
                pass.pixelShader = scaleShader;
                pass.parameters[0] = scale;
                pass.resolutionDivisor = divisor;
                pass.enabled = enabled;
                return pass;
            };
            std::array cases = {
                CaseResult{ "baseline", BASE_WIDTH, BASE_HEIGHT },
                CaseResult{ "repeat", BASE_WIDTH, BASE_HEIGHT },
                CaseResult{ "serial-recording", BASE_WIDTH, BASE_HEIGHT },
                CaseResult{ "odd-resize", 641, 359 },
                CaseResult{ "restored", BASE_WIDTH, BASE_HEIGHT },
                CaseResult{ .name = "disabled", .width = BASE_WIDTH, .height = BASE_HEIGHT },
                CaseResult{ .name = "hdr-copy", .width = BASE_WIDTH, .height = BASE_HEIGHT,
                    .expectedEffects = 8, .expectedDraws = 9 },
                CaseResult{ .name = "hdr-copy-repeat", .width = BASE_WIDTH, .height = BASE_HEIGHT,
                    .expectedEffects = 8, .expectedDraws = 9 },
                CaseResult{ .name = "ldr-copy", .width = BASE_WIDTH, .height = BASE_HEIGHT,
                    .exactBaseline = false, .expectedEffects = 2, .expectedDraws = 4 },
                CaseResult{ .name = "ordered-scales", .width = BASE_WIDTH, .height = BASE_HEIGHT,
                    .hdrScale = 0.5f, .ldrScale = 0.75f, .exactBaseline = false,
                    .expectedEffects = 2, .expectedDraws = 4 },
                CaseResult{ .name = "half-copy", .width = BASE_WIDTH, .height = BASE_HEIGHT,
                    .resolutionDivisor = 2, .exactBaseline = false, .expectedEffects = 1, .expectedDraws = 2 },
                CaseResult{ .name = "half-copy-repeat", .width = BASE_WIDTH, .height = BASE_HEIGHT,
                    .resolutionDivisor = 2, .exactBaseline = false, .expectedEffects = 1, .expectedDraws = 2 },
                CaseResult{ .name = "half-odd-resize", .width = 641, .height = 359,
                    .resolutionDivisor = 2, .exactBaseline = false, .expectedEffects = 1, .expectedDraws = 2 },
                CaseResult{ .name = "disabled-restored", .width = BASE_WIDTH, .height = BASE_HEIGHT },
                CaseResult{ .name = "empty-restored", .width = BASE_WIDTH, .height = BASE_HEIGHT },
            };
            DX12FrameCapture baseline;
            std::size_t previousTargetCount = 0;
            std::uint32_t baselineDescriptors = 0;
            for (CaseResult& test : cases)
            {
                const std::string_view caseName(test.name);
                const bool repeated = caseName == "hdr-copy-repeat" || caseName == "half-copy-repeat";
                std::vector<PostProcessPassDescription> passes;
                if (caseName == "hdr-copy")
                    passes.assign(8, scalePass(PostProcessStage::BeforeToneMapping, 1.0f));
                else if (caseName == "ldr-copy")
                    passes.assign(2, scalePass(PostProcessStage::AfterToneMapping, 1.0f));
                else if (caseName == "ordered-scales")
                {
                    // Registration order intentionally contradicts stage order.
                    passes.push_back(scalePass(PostProcessStage::AfterToneMapping, test.ldrScale));
                    passes.push_back(scalePass(PostProcessStage::BeforeToneMapping, test.hdrScale));
                }
                else if (test.resolutionDivisor != 1)
                    passes.push_back(scalePass(PostProcessStage::BeforeToneMapping, 1.0f, test.resolutionDivisor));
                else if (caseName == "disabled" || caseName == "disabled-restored")
                {
                    passes.push_back(scalePass(PostProcessStage::BeforeToneMapping, 0.0f, 1, false));
                    passes.push_back(scalePass(PostProcessStage::AfterToneMapping, 0.0f, 1, false));
                }
                if (!repeated && !renderer.setPostProcessPasses(passes))
                {
                    LOG_ERROR("[Validation] '{}' could not configure its post-process passes.", test.name);
                    return false;
                }
                if (caseName == "hdr-copy-repeat")
                {
                    auto invalid = scalePass(PostProcessStage::BeforeToneMapping, 0.0f, 0);
                    if (renderer.setPostProcessPasses(std::span(&invalid, 1)))
                    {
                        LOG_ERROR("[Validation] Invalid resolution was accepted.");
                        return false;
                    }
                    invalid.resolutionDivisor = 1;
                    invalid.parameters[0] = std::numeric_limits<float>::quiet_NaN();
                    if (renderer.setPostProcessPasses(std::span(&invalid, 1)))
                    {
                        LOG_ERROR("[Validation] Non-finite parameters were accepted.");
                        return false;
                    }
                    invalid.parameters[0] = 1.0f;
                    invalid.stage = static_cast<PostProcessStage>(99);
                    if (renderer.setPostProcessPasses(std::span(&invalid, 1)))
                    {
                        LOG_ERROR("[Validation] Invalid stage was accepted.");
                        return false;
                    }
                }
                renderer.setParallelModelRecordingEnabled(std::string_view(test.name) != "serial-recording");
                if (!renderer.resize(test.width, test.height))
                {
                    LOG_ERROR("[Validation] '{}' could not resize the renderer to {}x{}.",
                        test.name, test.width, test.height);
                    return false;
                }
                // Three frames exercise both in-flight slots before synchronous capture.
                const unsigned int frameCount = test.expectedEffects == 8 ? 30 : 3;
                for (unsigned int frame = 0; frame < frameCount; ++frame)
                    if (!scene.render(renderer, test.width, test.height))
                    {
                        LOG_ERROR("[Validation] '{}' failed while rendering frame {} of {}.",
                            test.name, frame + 1, frameCount);
                        return false;
                    }
                DX12FrameCapture captured;
                if (!renderer.captureFrame(captured))
                {
                    LOG_ERROR("[Validation] '{}' frame capture failed.", test.name);
                    return false;
                }
                if (!renderer.validateDebugMessages())
                {
                    LOG_ERROR("[Validation] '{}' produced a Direct3D diagnostic.", test.name);
                    return false;
                }
                if (!validateCapture(captured, test))
                    return false;
                const auto& statistics = renderer.getPostProcessStatistics();
                test.temporaryTargets = statistics.temporaryTargets;
                const auto descriptorCount = TextureManager::instance().getDescriptorHeap().getAllocatedCount();
                if (baselineDescriptors == 0)
                    baselineDescriptors = descriptorCount;
                const std::size_t targetLimit = test.expectedEffects > 1 ? 4 : 2;
                if (statistics.effectPasses != test.expectedEffects || statistics.drawCalls != test.expectedDraws
                    || (test.expectedEffects == 0 && statistics.temporaryTargets != 0)
                    || (test.expectedEffects != 0
                        && (statistics.temporaryTargets == 0 || statistics.temporaryTargets > targetLimit))
                    // The second in-flight frame may grow the pool until its first fence completes.
                    || (repeated && statistics.temporaryTargets < previousTargetCount)
                    || descriptorCount != baselineDescriptors + statistics.temporaryTargets)
                {
                    LOG_ERROR("[Validation] '{}' unexpected passes/draws/targets: {}/{}/{}.",
                        test.name, statistics.effectPasses, statistics.drawCalls, statistics.temporaryTargets);
                    return false;
                }
                previousTargetCount = statistics.temporaryTargets;
                if (test.exactBaseline && test.width == BASE_WIDTH && test.height == BASE_HEIGHT && baseline.hdr.GetImageCount() != 0
                    && (!identical(baseline.hdr, captured.hdr) || !identical(baseline.display, captured.display)))
                {
                    LOG_ERROR("[Validation] '{}' differs from the baseline.", test.name);
                    return false;
                }
                const std::wstring name(test.name, test.name + std::strlen(test.name));
                if (!captured.save(directory, name) || !verifySavedCapture(captured, directory, name))
                    return false;
                LOG_INFO("[Validation] '{}' passed: {}x{}, HDR peak {}, max display error {}.",
                    test.name, test.width, test.height, test.peakHdr, test.maxDisplayError);
                if (baseline.hdr.GetImageCount() == 0)
                {
                    baseline.hdr = std::move(captured.hdr);
                    baseline.display = std::move(captured.display);
                }
            }
            const CameraViewport cropped{ .x = 0.153f, .y = 0.137f, .width = 0.711f, .height = 0.697f };
            const std::array croppedPasses{
                scalePass(PostProcessStage::AfterToneMapping, 0.75f, 3),
                scalePass(PostProcessStage::BeforeToneMapping, 0.5f, 2),
            };
            DX12FrameCapture outsideReference;
            if (!renderer.resize(641, 359) || !renderer.setPostProcessPasses({})
                || !scene.render(renderer, 641, 359) || !renderer.captureFrame(outsideReference))
                return false;
            DX12FrameCapture identityReference;
            const std::array identityPasses{ scalePass(PostProcessStage::BeforeToneMapping, 1.0f),
                scalePass(PostProcessStage::BeforeToneMapping, 1.0f) };
            for (unsigned int enabled = 0; enabled < 2; ++enabled)
            {
                if (!renderer.setPostProcessPasses(enabled ? std::span(identityPasses) : std::span<const PostProcessPassDescription>{})
                    || !scene.render(renderer, 641, 359, cropped))
                    return false;
                DX12FrameCapture captured;
                if (!renderer.captureFrame(captured) || !renderer.validateDebugMessages())
                    return false;
                if (enabled && !identical(identityReference.display, captured.display))
                {
                    LOG_ERROR("[Validation] Full-resolution identity passes changed a fractional viewport.");
                    return false;
                }
                identityReference.display = std::move(captured.display);
            }
            if (!renderer.setPostProcessPasses(croppedPasses))
                return false;
            DX12FrameCapture croppedBaseline;
            for (unsigned int repeat = 0; repeat < 2; ++repeat)
            {
                for (unsigned int frame = 0; frame < 3; ++frame)
                    if (!scene.render(renderer, 641, 359, cropped))
                        return false;
                DX12FrameCapture captured;
                if (!renderer.captureFrame(captured) || !renderer.validateDebugMessages())
                    return false;
                const DirectX::Image& display = *captured.display.GetImage(0, 0, 0);
                for (const Probe& probe : PROBES)
                {
                    const auto x = static_cast<std::size_t>((cropped.x + probe.u * cropped.width) * 641);
                    const auto y = static_cast<std::size_t>((cropped.y + 0.5f * cropped.height) * 359);
                    const auto* const pixel = display.pixels + y * display.rowPitch + x * 4;
                    const std::array input{ probe.expected.x, probe.expected.y, probe.expected.z };
                    for (std::size_t channel = 0; channel < input.size(); ++channel)
                    {
                        const int expected = static_cast<int>(std::lround(displayReference(input[channel], 0.5f, 0.75f) * 255));
                        if (std::abs(static_cast<int>(pixel[channel]) - expected) > static_cast<int>(DISPLAY_TOLERANCE))
                        {
                            LOG_ERROR("[Validation] Cropped viewport probe '{}' channel {} expected {}, got {}.",
                                probe.name, channel, expected, pixel[channel]);
                            return false;
                        }
                    }
                }
                // A partial camera preserves the previous Game target outside its viewport.
                const DirectX::Image& outside = *outsideReference.display.GetImage(0, 0, 0);
                for (std::size_t y = 0; y < display.height; ++y)
                    for (std::size_t x = 0; x < display.width; ++x)
                        if (static_cast<float>(x) + 0.5f < cropped.x * 641
                            || static_cast<float>(x) + 0.5f >= (cropped.x + cropped.width) * 641
                            || static_cast<float>(y) + 0.5f < cropped.y * 359
                            || static_cast<float>(y) + 0.5f >= (cropped.y + cropped.height) * 359)
                        {
                            const auto* const pixel = display.pixels + y * display.rowPitch + x * 4;
                            if (std::memcmp(pixel, outside.pixels + y * outside.rowPitch + x * 4, 4) != 0)
                            {
                                LOG_ERROR("[Validation] PostEffect wrote outside the camera viewport.");
                                return false;
                            }
                        }
                if (repeat && !identical(croppedBaseline.display, captured.display))
                {
                    LOG_ERROR("[Validation] Cropped viewport reuse changed image contents.");
                    return false;
                }
                if (!repeat)
                {
                    if (!captured.save(directory, L"cropped-viewport")
                        || !verifySavedCapture(captured, directory, L"cropped-viewport"))
                        return false;
                    croppedBaseline.display = std::move(captured.display);
                }
            }
            if (!renderer.resize(BASE_WIDTH, BASE_HEIGHT))
                return false;
            // Optional effects must not leak into debug or legacy Forward/Nothing rendering.
            const std::array activePasses{
                scalePass(PostProcessStage::BeforeToneMapping, 0.5f),
                scalePass(PostProcessStage::AfterToneMapping, 0.75f),
            };
            for (const DeferredDebugView debugView : { DeferredDebugView::Albedo, DeferredDebugView::Emission })
            {
                DX12FrameCapture reference;
                for (unsigned int enabled = 0; enabled < 2; ++enabled)
                {
                    if (!renderer.setPostProcessPasses(enabled ? std::span(activePasses) : std::span<const PostProcessPassDescription>{})
                        || !renderer.setDeferredDebugView(debugView)
                        || !scene.render(renderer, BASE_WIDTH, BASE_HEIGHT))
                        return false;
                    DX12FrameCapture captured;
                    if (!renderer.captureFrame(captured) || !renderer.validateDebugMessages()
                        || renderer.getPostProcessStatistics().drawCalls != 0)
                        return false;
                    if (enabled && !identical(reference.display, captured.display))
                    {
                        LOG_ERROR("[Validation] PostEffect affected a G-Buffer debug view.");
                        return false;
                    }
                    reference.display = std::move(captured.display);
                }
            }
            if (!renderer.setDeferredDebugView(DeferredDebugView::FinalLighting))
                return false;
            for (const bool deferredEnabled : { false, true })
            {
                DX12FrameCapture reference;
                renderer.setDeferredRenderingEnabled(deferredEnabled);
                for (unsigned int enabled = 0; enabled < 2; ++enabled)
                {
                    // Prime Nothing's preserved color with identical, unprocessed output.
                    if (!renderer.setPostProcessPasses({})
                        || !scene.render(renderer, BASE_WIDTH, BASE_HEIGHT)
                        || !renderer.setPostProcessPasses(enabled ? std::span(activePasses) : std::span<const PostProcessPassDescription>{})
                        || !scene.render(renderer, BASE_WIDTH, BASE_HEIGHT, {},
                            deferredEnabled ? CameraClearMode::Nothing : CameraClearMode::SolidColor))
                        return false;
                    DX12FrameCapture captured;
                    if (!renderer.captureFrame(captured) || !renderer.validateDebugMessages()
                        || captured.hdr.GetImageCount() != 0 || renderer.getPostProcessStatistics().drawCalls != 0)
                        return false;
                    if (enabled && !identical(reference.display, captured.display))
                    {
                        LOG_ERROR("[Validation] PostEffect affected Forward/Nothing rendering.");
                        return false;
                    }
                    reference.display = std::move(captured.display);
                }
            }
            renderer.setDeferredRenderingEnabled(true);
            if (!renderer.setPostProcessPasses({})
                || TextureManager::instance().getDescriptorHeap().getAllocatedCount() != baselineDescriptors)
            {
                LOG_ERROR("[Validation] Temporary descriptors were not fully released.");
                return false;
            }
            // Zero-weight authoring snapshots are delivered but must preserve the baseline exactly.
            authoringSnapshot.weight = 0;
            if (!scene.render(renderer, BASE_WIDTH, BASE_HEIGHT, {}, CameraClearMode::SolidColor, authoringSnapshot))
                return false;
            DX12FrameCapture authored;
            if (!renderer.captureFrame(authored) || !renderer.validateDebugMessages()
                || renderer.getCameraPostProcessSnapshot() != authoringSnapshot
                || !identical(baseline.hdr, authored.hdr) || !identical(baseline.display, authored.display))
            {
                LOG_ERROR("[Validation] Zero-weight Camera snapshot affected baseline rendering or was not delivered.");
                return false;
            }
            if (!runPostProcessEffectsValidation(renderer, directory,
                [&scene, &renderer](const std::uint32_t width, const std::uint32_t height,
                    const CameraViewport& viewport, const PostProcessCameraSnapshot& camera, const PostProcessValidationFixture fixture)
                { return scene.render(renderer, width, height, viewport,
                    fixture == PostProcessValidationFixture::Nothing ? CameraClearMode::Nothing : CameraClearMode::SolidColor,
                    camera, fixture); },
                verifySavedCapture))
                return false;
            CustomEffectFixture customEffect;
            if (!runPostProcessCustomEffectValidation(renderer, directory,
                [&scene, &renderer](const std::uint32_t width, const std::uint32_t height,
                    const CameraViewport& viewport, const PostProcessCameraSnapshot& camera, const PostProcessValidationFixture fixture)
                { return scene.render(renderer, width, height, viewport, CameraClearMode::SolidColor, camera, fixture); },
                verifySavedCapture, customEffect.handle))
                return false;
            if (!runPostProcessVolumeValidation(renderer, directory,
                [&scene, &renderer](const std::uint32_t width, const std::uint32_t height, const CameraViewport& viewport,
                    const PostProcessCameraSnapshot& camera, const PostProcessValidationFixture fixture)
                { return scene.render(renderer, width, height, viewport, CameraClearMode::SolidColor, camera, fixture); },
                verifySavedCapture, customEffect.handle))
                return false;
            std::ofstream report(directory / L"results.json", std::ios::binary);
            report << "{\n  \"version\": 8,\n  \"passed\": true,\n"
                "  \"scene\": \"procedural-orthographic-panels\",\n"
                "  \"exposure\": 1,\n  \"hdrTolerance\": " << HDR_TOLERANCE
                << ",\n  \"displayToleranceBytes\": " << DISPLAY_TOLERANCE
                << ",\n  \"debugWarningsAndErrors\": 0,\n"
                "  \"savedImagesMatchReadback\": true,\n  \"cases\": [\n";
            for (std::size_t index = 0; index < cases.size(); ++index)
            {
                const CaseResult& test = cases[index];
                report << "    {\"name\": \"" << test.name << "\", \"width\": " << test.width
                    << ", \"height\": " << test.height << ", \"peakHdr\": " << test.peakHdr
                    << ", \"maxDisplayErrorBytes\": " << test.maxDisplayError
                    << ", \"effectPasses\": " << test.expectedEffects << ", \"drawCalls\": " << test.expectedDraws
                    << ", \"temporaryTargets\": " << test.temporaryTargets << "}"
                    << (index + 1 < cases.size() ? ",\n" : "\n");
            }
            report << "  ],\n  \"invalidConfigurationsPreservedPipeline\": true,\n"
                "  \"debugAndForwardBypass\": true,\n  \"croppedViewport\": true,\n"
                "  \"temporaryDescriptorsFullyReleased\": true,\n"
                "  \"authoringReport\": \"authoring-results.json\",\n"
                "  \"effectsReport\": \"effects-results.json\",\n"
                "  \"customEffectsReport\": \"custom-effects-results.json\",\n"
                "  \"volumeReport\": \"volume-results.json\",\n"
                "  \"phase6Report\": \"phase6-results.json\",\n"
                "  \"phase7Report\": \"phase7-results.json\",\n"
                "  \"cameraAuthoringSnapshotPreservedBaseline\": true\n}\n";
            report.close();
            if (!report)
            {
                LOG_ERROR("[Validation] Cannot save results.json.");
                return false;
            }
            LOG_INFO("[Validation] All Phase 0/1/2/3/4/5/6/7 checks passed. Captures: '{}'.", directory.string());
            return true;
        }
    }

    bool runRenderingValidation(DX12Renderer& renderer, const std::filesystem::path& directory)
    {
        try
        {
            return runValidation(renderer, directory);
        }
        catch (const std::bad_alloc& exception)
        {
            LOG_ERROR("[Validation] Allocation failed: {}.", exception.what());
        }
        catch (const std::length_error& exception)
        {
            LOG_ERROR("[Validation] Capacity exceeded: {}.", exception.what());
        }
        return false;
    }
}
