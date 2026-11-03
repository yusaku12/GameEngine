#include "Pch.h"
#include "Terrain\TerrainValidation.h"
#include "Terrain\TerrainMeshBuilder.h"
#include "Assets\Model\ModelManager.h"
#include "Core\GameObject\Component\TerrainComponent.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\System\StartupOptions.h"
#include "Generated\FlatBuffers\TerrainComponent_generated.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\DirectX12\FrameCapture.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <imgui.h>
#include <wincodec.h>

namespace Engine
{
    namespace
    {
        struct Checks
        {
            std::size_t count = 0;
            bool expect(const bool condition, const char* label)
            {
                if (!condition)
                    LOG_ERROR("[TerrainValidation] FAILED: {}.", label);
                else
                    ++count;
                return condition;
            }
            bool close(const float actual, const float expected, const char* label)
            {
                return expect(std::isfinite(actual) && std::abs(actual - expected) < 0.00002f, label);
            }
        };

        bool sameHeights(const Heightmap& left, const Heightmap& right)
        {
            return left.width() == right.width() && left.height() == right.height()
                && std::equal(left.samples().begin(), left.samples().end(), right.samples().begin(), right.samples().end());
        }

        bool validateHeightmap(Checks& checks, const std::filesystem::path& directory, Heightmap& imported)
        {
            Heightmap map;
            const Heightmap original = map;
            const std::array<float, 4> valid = { 0.0f, 0.25f, 0.75f, 1.0f };
            for (const std::uint32_t dimension : { 0u, 1u, Heightmap::MAX_DIMENSION + 1, UINT32_MAX })
            {
                if (!checks.expect(!map.createFlat(dimension, 2) && !map.createFlat(2, dimension)
                    && !map.assign(dimension, 2, valid) && sameHeights(map, original), "invalid dimensions preserve heights"))
                    return false;
            }
            for (const float invalid : { -0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
            {
                auto samples = valid;
                samples[1] = invalid;
                if (!checks.expect(!map.assign(2, 2, samples) && !map.createFlat(2, 2, invalid)
                    && sameHeights(map, original), "invalid normalized sample preserves heights"))
                    return false;
            }
            if (!checks.expect(!map.assign(2, 2, {}) && !map.assign(3, 2, valid)
                && sameHeights(map, original), "sample count must match dimensions")
                || !checks.expect(map.assign(2, 2, valid) && map.assign(2, 2, map.samples()), "self-referential assignment"))
                return false;

            const auto save = [](const DirectX::ScratchImage& image, const std::filesystem::path& path,
                const GUID& pixelFormat)
            {
                return SUCCEEDED(DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
                    DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str(), &pixelFormat));
            };
            DirectX::ScratchImage image8;
            DirectX::ScratchImage image16;
            if (!checks.expect(SUCCEEDED(image8.Initialize2D(DXGI_FORMAT_R8_UNORM, 3, 2, 1, 1))
                && SUCCEEDED(image16.Initialize2D(DXGI_FORMAT_R16_UNORM, 3, 2, 1, 1)), "allocate import fixtures"))
                return false;
            const std::array<std::uint8_t, 6> values8 = { 0, 1, 127, 128, 254, 255 };
            const std::array<std::uint16_t, 6> values16 = { 0, 1, 257, 32768, 50000, 65535 };
            for (std::size_t row = 0; row < 2; ++row)
            {
                const auto* a = image8.GetImage(0, 0, 0);
                const auto* b = image16.GetImage(0, 0, 0);
                std::memcpy(a->pixels + row * a->rowPitch, values8.data() + row * 3, 3);
                std::memcpy(b->pixels + row * b->rowPitch, values16.data() + row * 3, 3 * sizeof(std::uint16_t));
            }
            if (!checks.expect(save(image8, directory / L"height8.png", GUID_WICPixelFormat8bppGray)
                && save(image16, directory / L"height16.png", GUID_WICPixelFormat16bppGray), "write 8/16-bit PNG fixtures")
                || !checks.expect(map.loadImage(directory / L"height8.png")
                    && map.width() == 3 && map.height() == 2, "import rectangular 8-bit PNG")
                || !checks.expect(imported.loadImage(directory / L"height16.png")
                    && imported.width() == 3 && imported.height() == 2, "import rectangular 16-bit PNG"))
                return false;
            for (std::size_t index = 0; index < values8.size(); ++index)
                if (!checks.close(map.samples()[index], static_cast<float>(values8[index]) / 255.0f, "8-bit sample orientation")
                    || !checks.close(imported.samples()[index], static_cast<float>(values16[index]) / 65535.0f, "16-bit sample precision"))
                    return false;
            if (!checks.expect(imported.samples()[1] > 0.0f && imported.samples()[1] < 1.0f / 255.0f,
                "16-bit data is not quantized to 8 bits"))
                return false;

            DirectX::ScratchImage color;
            if (!checks.expect(SUCCEEDED(color.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 2, 2, 1, 1)),
                "allocate sRGB fixture"))
                return false;
            const auto* pixels = color.GetImage(0, 0, 0);
            for (std::size_t row = 0; row < 2; ++row)
                for (std::size_t column = 0; column < 2; ++column)
                {
                    const std::array<std::uint8_t, 4> rgba = { 128, 17, 240, 1 };
                    std::memcpy(pixels->pixels + row * pixels->rowPitch + column * 4, rgba.data(), rgba.size());
                }
            if (!checks.expect(save(color, directory / L"red-channel.png", GUID_WICPixelFormat32bppBGRA)
                && map.loadImage(directory / L"red-channel.png"), "import red-channel image")
                || !checks.close(map.samples()[0], 128.0f / 255.0f, "ignore gamma, green, blue and alpha"))
                return false;
            for (const std::size_t width : { 1u, Heightmap::MAX_DIMENSION + 1 })
            {
                DirectX::ScratchImage invalid;
                if (!checks.expect(SUCCEEDED(invalid.Initialize2D(DXGI_FORMAT_R8_UNORM, width, 2, 1, 1)),
                    "allocate invalid-size fixture"))
                    return false;
                std::memset(invalid.GetPixels(), 0, invalid.GetPixelsSize());
                const auto path = directory / (width == 1 ? L"too-small.png" : L"too-large.png");
                const Heightmap previous = map;
                if (!checks.expect(save(invalid, path, GUID_WICPixelFormat8bppGray) && !map.loadImage(path)
                    && sameHeights(map, previous), "reject invalid image dimensions without replacing data"))
                    return false;
            }
            const Heightmap previous = map;
            std::ofstream(directory / L"invalid.png") << "not an image";
            return checks.expect(!map.loadImage(directory / L"missing.png") && !map.loadImage(directory / L"invalid.png")
                && sameHeights(map, previous), "missing/corrupt image preserves data");
        }

        bool validateMesh(Checks& checks)
        {
            Heightmap heightmap;
            ModelResource model;
            if (!checks.expect(buildTerrainModel(heightmap, {}, model), "build minimal flat terrain"))
                return false;
            const std::vector<std::uint32_t> indices = { 0, 2, 1, 1, 2, 3 };
            if (!checks.expect(model.meshes.size() == 1 && model.meshes[0].vertices.size() == 4
                && model.meshes[0].indices == indices && model.meshes[0].subMeshes.size() == 1
                && model.meshes[0].subMeshes[0].indexCount == 6 && model.nodes[0].meshIndices[0] == 0,
                "minimal topology and model node"))
                return false;
            for (const auto& vertex : model.meshes[0].vertices)
                if (!checks.close(vertex.normal.y, 1.0f, "flat upward normal")
                    || !checks.close(vertex.tangent.x, 1.0f, "flat positive X tangent")
                    || !checks.close(vertex.bitangent.z, 1.0f, "flat positive Z bitangent"))
                    return false;

            const std::array<float, 6> ramp = { 0.0f, 0.2f, 0.4f, 0.2f, 0.4f, 0.6f };
            TerrainMeshSettings settings{ 4, 2, 10, -3, 3, 5 };
            if (!checks.expect(heightmap.assign(3, 2, ramp) && buildTerrainModel(heightmap, settings, model),
                "build rectangular planar ramp"))
                return false;
            const auto& mesh = model.meshes[0];
            const Vector3 expectedNormal = Vector3(-1, 1, -1) / std::sqrt(3.0f);
            if (!checks.expect(mesh.vertices.size() == 6 && mesh.indices.size() == 12, "rectangular topology")
                || !checks.close(mesh.vertices.front().position.x, -2, "centered minimum X")
                || !checks.close(mesh.vertices.back().position.z, 1, "centered maximum Z")
                || !checks.close(mesh.vertices.front().position.y, -3, "height offset")
                || !checks.close(mesh.vertices.back().position.y, 3, "height scale")
                || !checks.close(mesh.vertices.back().texCoord.x, 3, "U repeat")
                || !checks.close(mesh.vertices.back().texCoord.y, 5, "V repeat")
                || !checks.close(mesh.boundingBox.Center.y, 0, "AABB center")
                || !checks.close(mesh.boundingBox.Extents.y, 3, "AABB height extent"))
                return false;
            for (const auto& vertex : mesh.vertices)
                if (!checks.close(vertex.normal.Dot(expectedNormal), 1.0f, "ramp normal includes boundaries")
                    || !checks.close(vertex.normal.Dot(vertex.tangent), 0.0f, "normal/tangent orthogonality")
                    || !checks.close(vertex.normal.Dot(vertex.bitangent), 0.0f, "normal/bitangent orthogonality")
                    || !checks.close(vertex.tangent.Dot(vertex.bitangent), 0.0f, "tangent/bitangent orthogonality")
                    || !checks.close(vertex.normal.Length(), 1.0f, "unit normal")
                    || !checks.close(vertex.tangent.Length(), 1.0f, "unit tangent")
                    || !checks.close(vertex.bitangent.Length(), 1.0f, "unit bitangent")
                    || !checks.expect(mesh.boundingSphere.Contains(vertex.position) != DirectX::DISJOINT, "bounds enclose vertices"))
                    return false;
            for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
            {
                const Vector3 a = mesh.vertices[mesh.indices[index]].position;
                const Vector3 b = mesh.vertices[mesh.indices[index + 1]].position;
                const Vector3 c = mesh.vertices[mesh.indices[index + 2]].position;
                if (!checks.expect((b - a).Cross(c - a).y > 0.0f, "triangle winding faces +Y"))
                    return false;
            }

            using Field = float TerrainMeshSettings::*;
            const std::array<Field, 6> fields = { &TerrainMeshSettings::sizeX, &TerrainMeshSettings::sizeZ,
                &TerrainMeshSettings::heightScale, &TerrainMeshSettings::heightOffset,
                &TerrainMeshSettings::uvRepeatX, &TerrainMeshSettings::uvRepeatZ };
            for (const auto field : fields)
                for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity(), -100001.0f, 100001.0f })
                {
                    TerrainMeshSettings bad = settings;
                    bad.*field = invalid;
                    if (!checks.expect(!buildTerrainModel(heightmap, bad, model) && model.meshes[0].vertices.size() == 6
                        && model.meshes[0].vertices.back().position.y == 3, "invalid settings preserve output"))
                        return false;
                }
            const std::array<float, 4> steep = { 0, 1, 1, 0 };
            if (!checks.expect(heightmap.assign(2, 2, steep)
                && buildTerrainModel(heightmap, { 0.01f, 0.01f, 100000, -100000, 0.001f, 10000 }, model),
                "build extreme height/size range"))
                return false;
            for (const auto& vertex : model.meshes[0].vertices)
                if (!checks.close(vertex.normal.Length(), 1, "extreme unit normal")
                    || !checks.close(vertex.tangent.Length(), 1, "extreme unit tangent")
                    || !checks.close(vertex.bitangent.Length(), 1, "extreme unit bitangent"))
                    return false;
            if (!checks.expect(heightmap.createFlat(Heightmap::MAX_DIMENSION, 2)
                && buildTerrainModel(heightmap, {}, model), "maximum axis dimension")
                || !checks.expect(heightmap.createFlat(257, 257) && buildTerrainModel(heightmap, {}, model)
                    && *std::max_element(model.meshes[0].indices.begin(), model.meshes[0].indices.end()) == 66048,
                    "32-bit indices exceed 65535 without truncation"))
                return false;
            if (!checks.expect(heightmap.createFlat(Heightmap::MAX_DIMENSION, Heightmap::MAX_DIMENSION)
                && buildTerrainModel(heightmap, {}, model) && model.meshes[0].vertices.size() == 1050625
                && model.meshes[0].indices.size() == 6291456 && model.meshes[0].subMeshes[0].indexCount == 6291456
                && *std::max_element(model.meshes[0].indices.begin(), model.meshes[0].indices.end()) == 1050624,
                "maximum supported 1025x1025 grid has exact counts and index range"))
                return false;
            return true;
        }

        bool validateComponent(Checks& checks, const std::filesystem::path& directory, const Heightmap& imported)
        {
            Scene source(ObjectGUID::generate(), "Terrain source");
            GameObject* object = source.createGameObject("Terrain");
            auto* terrain = object != nullptr ? object->addComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(terrain != nullptr, "Terrain registry and construction"))
                return false;
            const TerrainMeshSettings settings{ 4, 2, 3, -1, 2, 4 };
            if (!checks.expect(terrain->setTerrain(imported, settings), "component generation"))
                return false;
            terrain->setCastShadows(false);
            std::vector<std::uint8_t> payload;
            if (!checks.expect(terrain->serializePayload(payload), "serialize terrain payload"))
                return false;
            const ModelHandle original = terrain->getModel();
            for (const std::size_t size : { 0u, 1u, 4u, 7u })
                if (!checks.expect(!terrain->deserializePayload(1, std::span(payload).first(size))
                    && terrain->getModel() == original, "truncated payload preserves terrain"))
                    return false;
            if (!checks.expect(!terrain->deserializePayload(2, payload), "reject unknown payload version"))
                return false;
            flatbuffers::FlatBufferBuilder builder;
            const std::array<float, 4> invalidSamples = { 0, 0, 0, std::numeric_limits<float>::quiet_NaN() };
            const auto root = Serialization::CreateTerrainComponentPayload(builder, 2, 2,
                builder.CreateVector(invalidSamples.data(), invalidSamples.size()));
            Serialization::FinishTerrainComponentPayloadBuffer(builder, root);
            if (!checks.expect(!terrain->deserializePayload(1, { builder.GetBufferPointer(), builder.GetSize() })
                && terrain->getModel() == original && sameHeights(terrain->getHeightmap(), imported),
                "valid FlatBuffer with invalid values is rejected transactionally"))
                return false;
            for (const std::uint32_t width : { 0u, 3u, UINT32_MAX })
            {
                flatbuffers::FlatBufferBuilder malformed;
                const std::array<float, 4> validSamples{};
                const auto malformedRoot = Serialization::CreateTerrainComponentPayload(malformed, width, 2,
                    malformed.CreateVector(validSamples.data(), validSamples.size()));
                Serialization::FinishTerrainComponentPayloadBuffer(malformed, malformedRoot);
                if (!checks.expect(!terrain->deserializePayload(1, { malformed.GetBufferPointer(), malformed.GetSize() })
                    && terrain->getModel() == original, "invalid serialized dimensions and sample counts preserve terrain"))
                    return false;
            }

            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"terrain.scene", source),
                "save embedded terrain scene"))
                return false;
            std::error_code error;
            if (!checks.expect(std::filesystem::remove(directory / L"height16.png", error) && !error,
                "remove source height image before scene reload"))
                return false;
            Scene restored(ObjectGUID::generate(), "Restored");
            if (!checks.expect(Serialization::SceneSerializer{}.load(directory / L"terrain.scene", restored), "reload terrain without image"))
                return false;
            const GameObject* loaded = restored.find(object->getGUID());
            const auto* restoredTerrain = loaded != nullptr ? loaded->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(restoredTerrain != nullptr && sameHeights(restoredTerrain->getHeightmap(), imported)
                && restoredTerrain->getSettings().uvRepeatZ == settings.uvRepeatZ
                && !restoredTerrain->getCastShadows() && restoredTerrain->getModel() != terrain->getModel(),
                "scene roundtrip preserves samples, settings and shadow flag"))
                return false;
            Scene clone(ObjectGUID::generate(), "Clone");
            const SceneClonePair pair{ source, clone };
            if (!checks.expect(cloneScenes(std::span(&pair, 1)), "Play/duplicate scene clone"))
                return false;
            auto* cloned = clone.find(object->getGUID())->getComponent<TerrainComponent>();
            if (!checks.expect(cloned != nullptr && cloned->getModel() != terrain->getModel()
                && sameHeights(cloned->getHeightmap(), imported) && !cloned->getCastShadows(), "clone owns independent geometry"))
                return false;

            Prefab prefab;
            Prefab reloaded;
            Scene prefabScene(ObjectGUID::generate(), "Prefab");
            if (!checks.expect(prefab.capture(*object) && Serialization::PrefabSerializer{}.save(directory / L"terrain.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"terrain.prefab", reloaded), "prefab persistence"))
                return false;
            auto* instance = reloaded.instantiate(prefabScene);
            auto* instanceTerrain = instance != nullptr ? instance->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(instanceTerrain != nullptr && sameHeights(instanceTerrain->getHeightmap(), imported)
                && instanceTerrain->getModel() != terrain->getModel(), "independent terrain prefab instance"))
                return false;
            const auto instanceModel = instanceTerrain->getModel();
            prefabScene.clear();
            if (!checks.expect(ModelManager::instance().get(instanceModel) == nullptr, "terrain destruction unloads model"))
                return false;

            auto& queue = ModelRenderSubmissionQueue::instance();
            queue.clear();
            std::vector<ModelRenderSubmission> submissions;
            object->setLayer(3);
            object->getTransform()->setPosition(Vector3(2, 3, 4));
            source.submitRenderData();
            queue.consume(submissions);
            if (!checks.expect(submissions.size() == 1 && submissions[0].modelSnapshot != nullptr
                && submissions[0].model == original && submissions[0].objectID != 0
                && submissions[0].layer == 3 && !submissions[0].castShadows, "editor render submission"))
                return false;
            AABB expected;
            submissions[0].modelSnapshot->boundingBox.Transform(expected, object->getWorldMatrix());
            if (!checks.close(submissions[0].worldBounds.Center.y, expected.Center.y, "transformed terrain bounds"))
                return false;
            Heightmap flat;
            if (!checks.expect(flat.createFlat(3, 3, 0.5f) && terrain->setTerrain(flat, settings)
                && ModelManager::instance().get(original) == nullptr && submissions[0].modelSnapshot->meshes.size() == 1
                && sameHeights(cloned->getHeightmap(), imported), "rebuild releases handle but retains queued immutable geometry"))
                return false;
            terrain->setEnabled(false);
            source.submitRenderData();
            queue.consume(submissions);
            if (!checks.expect(submissions.empty(), "disabled terrain submits nothing"))
                return false;
            terrain->setEnabled(true);
            object->setActive(false);
            source.submitRenderData();
            queue.consume(submissions);
            if (!checks.expect(submissions.empty(), "inactive terrain submits nothing"))
                return false;
            object->setActive(true);
            source.update(0.0f);
            source.lateUpdate(0.0f);
            queue.consume(submissions);
            if (!checks.expect(submissions.size() == 1, "runtime lifecycle submits terrain exactly once"))
                return false;
            const auto finalHandle = terrain->getModel();
            source.clear();
            return checks.expect(ModelManager::instance().get(finalHandle) == nullptr, "runtime terrain cleanup");
        }

        bool validateInspector(Checks& checks)
        {
            struct Context
            {
                ImGuiContext* previous = ImGui::GetCurrentContext();
                std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> owned{
                    ImGui::CreateContext(), &ImGui::DestroyContext };
                ~Context() { owned.reset(); ImGui::SetCurrentContext(previous); }
            } context;
            if (!checks.expect(context.owned != nullptr, "terrain inspector context"))
                return false;
            auto& io = ImGui::GetIO();
            io.IniFilename = io.LogFilename = nullptr;
            io.DisplaySize = { 1600, 2000 };
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            io.Fonts->SetTexID(static_cast<ImTextureID>(1));
            TerrainComponent terrain;
            ImVec2 rebuildButton;
            const auto frame = [&]
            {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({ 10, 10 }, ImGuiCond_Always);
                ImGui::SetNextWindowSize({ 1500, 1900 }, ImGuiCond_Always);
                ImGui::Begin("Terrain Inspector Validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
                const auto origin = ImGui::GetCursorScreenPos();
                terrain.drawImGui();
                rebuildButton = { origin.x + 30, origin.y + 3 * ImGui::GetTextLineHeightWithSpacing()
                    + 7 * ImGui::GetFrameHeightWithSpacing() + ImGui::GetFrameHeight() * 0.5f };
                ImGui::End();
                ImGui::Render();
            };
            frame();
            io.AddMousePosEvent(rebuildButton.x, rebuildButton.y); frame();
            io.AddMouseButtonEvent(0, true); frame();
            io.AddMouseButtonEvent(0, false); frame();
            return checks.expect(terrain.getModel().isValid() && terrain.getHeightmap().width() == 65,
                "Inspector Apply/Rebuild button generates terrain");
        }

        bool renderFrame(DX12Renderer& renderer, Scene* scene)
        {
            RenderView view;
            view.camera.position = Vector3(0, 20, 0);
            view.camera.forward = Vector3(0, -1, 0);
            view.camera.view = Matrix(DirectX::XMMatrixLookAtLH(view.camera.position, Vector3::Zero, Vector3::UnitZ));
            view.camera.projection = Matrix(DirectX::XMMatrixOrthographicLH(8, 8, 0.1f, 100));
            view.camera.viewProjection = view.camera.view * view.camera.projection;
            view.camera.previousViewProjection = view.camera.viewProjection;
            view.camera.inverseView = view.camera.view.Invert();
            view.camera.inverseProjection = view.camera.projection.Invert();
            view.camera.inverseViewProjection = view.camera.viewProjection.Invert();
            view.camera.viewportSize = Vector2(256, 256);
            view.camera.inverseViewportSize = Vector2(1.0f / 256, 1.0f / 256);
            view.camera.nearClip = 0.1f;
            view.camera.farClip = 100;
            view.backgroundColor = Color(0.01f, 0.02f, 0.04f, 1);
            CameraRenderSubmissionQueue::instance().submit(view);
            if (scene != nullptr)
                scene->submitRenderData();
            FrameProfiler& profiler = FrameProfiler::instance();
            const std::uint64_t frameID = profiler.beginFrame();
            if (frameID == 0)
                return false;
            const auto start = std::chrono::steady_clock::now();
            const bool rendered = renderer.render();
            const double duration = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            const bool measured = profiler.endFrame(frameID, duration);
            return rendered && measured;
        }

        bool validateRendering(Checks& checks, DX12Renderer& renderer, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "GPU terrain");
            auto* object = scene.createGameObject("Terrain");
            auto* terrain = object != nullptr ? object->addComponent<TerrainComponent>() : nullptr;
            Heightmap heights;
            const std::array<float, 9> hill = { 0, 0.2f, 0, 0.2f, 1, 0.2f, 0, 0.2f, 0 };
            const TerrainMeshSettings settings{ 4, 4, 2, 0, 1, 1 };
            if (!checks.expect(terrain != nullptr && heights.assign(3, 3, hill) && terrain->setTerrain(heights, settings),
                "GPU terrain fixture") || !renderer.resize(256, 256))
                return false;
            renderer.setEditorOverlayEnabled(false);
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(renderFrame(renderer, nullptr), "render background reference"))
                    return false;
            DX12FrameCapture background;
            if (!checks.expect(renderer.captureFrame(background), "capture background"))
                return false;
            for (int frame = 0; frame < 3; ++frame)
                if (!checks.expect(renderFrame(renderer, &scene), "render generated terrain"))
                    return false;
            DX12FrameCapture capture;
            if (!checks.expect(renderer.captureFrame(capture) && renderer.validateDebugMessages(), "GPU capture without diagnostics"))
                return false;
            const auto* before = background.display.GetImage(0, 0, 0);
            const auto* after = capture.display.GetImage(0, 0, 0);
            if (!checks.expect(before != nullptr && after != nullptr && after->format == DXGI_FORMAT_R8G8B8A8_UNORM
                && before->format == after->format && before->width == after->width, "capture shape"))
                return false;
            std::size_t changed = 0;
            for (std::size_t row = 0; row < after->height; ++row)
                for (std::size_t column = 0; column < after->width; ++column)
                    if (std::memcmp(before->pixels + row * before->rowPitch + column * 4,
                        after->pixels + row * after->rowPitch + column * 4, 3) != 0)
                        ++changed;
            if (!checks.expect(changed > 15000 && changed < 18000, "up-facing terrain covers expected 128x128 pixel area")
                || !checks.expect(capture.save(directory, L"terrain"), "save terrain HDR and display captures"))
                return false;

            // Destroy the model before its first GPU upload; the queued snapshot must still render.
            if (!checks.expect(terrain->setTerrain(heights, settings), "new unuploaded terrain"))
                return false;
            scene.submitRenderData();
            const ModelHandle retired = terrain->getModel();
            scene.clear();
            if (!checks.expect(ModelManager::instance().get(retired) == nullptr && renderFrame(renderer, nullptr)
                && renderer.captureFrame(capture) && renderer.validateDebugMessages(), "queued terrain survives component destruction"))
                return false;
            const auto* retained = capture.display.GetImage(0, 0, 0);
            if (!checks.expect(retained != nullptr && std::memcmp(retained->pixels + 128 * retained->rowPitch + 128 * 4,
                before->pixels + 128 * before->rowPitch + 128 * 4, 3) != 0, "retired terrain actually draws"))
                return false;

            for (int frame = 0; frame < 6; ++frame)
                if (!checks.expect(renderFrame(renderer, nullptr), "drain in-flight terrain resources"))
                    return false;
            renderer.setDeferredRenderingEnabled(false);
            auto* forwardObject = scene.createGameObject("Forward terrain");
            auto* forward = forwardObject != nullptr ? forwardObject->addComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(forward != nullptr && forward->setTerrain(heights, settings), "forward terrain fixture"))
                return false;
            renderer.setOcclusionCullingEnabled(true);
            for (int frame = 0; frame < 4; ++frame)
                if (!checks.expect(renderFrame(renderer, &scene), "forward and occlusion terrain rendering"))
                    return false;
            if (!checks.expect(renderer.captureFrame(capture) && renderer.validateDebugMessages(), "forward terrain GPU diagnostics"))
                return false;
            const auto* forwardImage = capture.display.GetImage(0, 0, 0);
            return checks.expect(forwardImage != nullptr && std::memcmp(forwardImage->pixels + 128 * forwardImage->rowPitch + 128 * 4,
                before->pixels + 128 * before->rowPitch + 128 * 4, 3) != 0, "forward terrain actually draws");
        }

        bool validateOptions(Checks& checks)
        {
            StartupOptions options;
            std::string error;
            const std::array<std::wstring_view, 2> valid = { L"--validate-terrain", L"output" };
            const std::array<std::wstring_view, 1> missing = { L"--validate-terrain" };
            const std::array<std::wstring_view, 3> runtime = { L"--runtime", L"--validate-terrain", L"output" };
            const std::array<std::wstring_view, 4> combined = {
                L"--validate-terrain", L"output", L"--validate-rendering", L"other" };
            return checks.expect(options.parse(valid, error) && options.mode == ExecutionMode::Runtime
                && options.terrainValidationDirectory == L"output", "terrain CLI option")
                && checks.expect(!options.parse(missing, error) && !options.parse(runtime, error)
                    && !options.parse(combined, error), "terrain CLI rejects missing and conflicting options");
        }
    }

    bool runTerrainValidation(DX12Renderer& renderer, const std::filesystem::path& directory)
    {
        struct ComApartment
        {
            HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
        } apartment;
        try
        {
            if (FAILED(apartment.result) || !renderer.validateDebugMessages())
            {
                LOG_ERROR("[TerrainValidation] COM and D3D12 Debug Layer are required.");
                return false;
            }
            std::error_code error;
            const bool exists = std::filesystem::exists(directory, error);
            if (error || (exists && (!std::filesystem::is_directory(directory, error)
                || !std::filesystem::is_empty(directory, error))) || error
                || (!exists && !std::filesystem::create_directories(directory, error)) || error)
            {
                LOG_ERROR("[TerrainValidation] Output must be a new or empty directory: '{}'.", directory.string());
                return false;
            }
            Checks checks;
            Heightmap imported;
            if (!validateOptions(checks) || !validateHeightmap(checks, directory, imported) || !validateMesh(checks)
                || !validateComponent(checks, directory, imported) || !validateInspector(checks)
                || !validateRendering(checks, renderer, directory))
                return false;
            std::ofstream report(directory / L"terrain-results.txt");
            report << "PASS: " << checks.count << " terrain checks\n"
                << "8/16-bit image precision, mesh geometry, scene/prefab/clone, Inspector, lifecycle,\n"
                << "Deferred/Forward rendering, queued geometry lifetime and D3D12 diagnostics.\n";
            report.close();
            if (!report)
            {
                LOG_ERROR("[TerrainValidation] Cannot write validation results.");
                return false;
            }
            LOG_INFO("[TerrainValidation] PASS: {} checks.", checks.count);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[TerrainValidation] Insufficient memory.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[TerrainValidation] Collection capacity exceeded.");
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            LOG_ERROR("[TerrainValidation] Filesystem error: {}.", error.what());
        }
        return false;
    }
}
