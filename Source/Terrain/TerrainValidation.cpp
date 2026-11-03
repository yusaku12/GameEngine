#include "Pch.h"
#include "Terrain\TerrainValidation.h"
#include "Terrain\TerrainCollisionValidation.h"
#include "Terrain\TerrainLodValidation.h"
#include "Terrain\TerrainBackgroundValidation.h"
#include "Terrain\TerrainErosionValidation.h"
#include "Terrain\TerrainGpuValidation.h"
#include "Terrain\TerrainGpuNoise.h"
#include "Terrain\TerrainMeshBuilder.h"
#include "Terrain\TerrainMaterialSerialization.h"
#include "Assets\Material\MaterialManager.h"
#include "Assets\Material\Serialization\MaterialSerializer.h"
#include "Assets\Model\ModelManager.h"
#include "Assets\Model\Serialization\ModelSerializer.h"
#include "Core\GameObject\Component\TerrainComponent.h"
#include "Core\Prefab\PrefabSerializer.h"
#include "Core\Profiling\FrameProfiler.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\SceneSerializer.h"
#include "Core\System\StartupOptions.h"
#include "Core\Threading\JobSystem.h"
#include "Generated\FlatBuffers\TerrainComponent_generated.h"
#include "Graphics\Camera\CameraRenderSubmission.h"
#include "Graphics\DirectX12\FrameCapture.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Graphics\Renderer\ModelRenderSubmission.h"
#include "Graphics\Texture\TextureManager.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <imgui.h>
#include <imgui_internal.h>
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

        bool normalizedHeights(const Heightmap& heightmap)
        {
            return std::all_of(heightmap.samples().begin(), heightmap.samples().end(), [](const float value)
            {
                return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
            });
        }

        bool validateNoise(Checks& checks)
        {
            TerrainNoiseSettings settings;
            settings.seed = 42;
            settings.width = 33;
            settings.height = 17;
            settings.offsetX = -2.25f;
            settings.offsetZ = 3.75f;
            Heightmap perlin;
            Heightmap fbm;
            Heightmap repeated;
            settings.mode = TerrainNoiseMode::Perlin;
            if (!checks.expect(generateTerrainNoise(settings, perlin) && generateTerrainNoise(settings, repeated)
                && sameHeights(perlin, repeated) && normalizedHeights(perlin), "seeded Perlin repeatability and bounds"))
                return false;
            settings.mode = TerrainNoiseMode::FBM;
            if (!checks.expect(generateTerrainNoise(settings, fbm) && generateTerrainNoise(settings, repeated)
                && sameHeights(fbm, repeated) && normalizedHeights(fbm) && !sameHeights(fbm, perlin),
                "seeded FBM repeatability and additional detail"))
                return false;
            const auto [minimum, maximum] = std::minmax_element(fbm.samples().begin(), fbm.samples().end());
            if (!checks.expect(*maximum - *minimum > 0.05f, "default FBM has measurable relief"))
                return false;
            settings.seed = 43;
            if (!checks.expect(generateTerrainNoise(settings, repeated) && !sameHeights(fbm, repeated), "different seeds change heights"))
                return false;
            settings.seed = 42;
            settings.octaves = 1;
            if (!checks.expect(generateTerrainNoise(settings, repeated) && sameHeights(repeated, perlin),
                "one-octave FBM equals Perlin"))
                return false;
            settings.octaves = 12;
            settings.persistence = 0;
            if (!checks.expect(generateTerrainNoise(settings, repeated) && sameHeights(repeated, perlin),
                "zero persistence equals Perlin without zero denominator"))
                return false;
            settings.persistence = 1;
            settings.lacunarity = 1;
            if (!checks.expect(generateTerrainNoise(settings, repeated) && sameHeights(repeated, perlin),
                "unit lacunarity repeats the same field without amplitude growth"))
                return false;

            settings = {};
            settings.mode = TerrainNoiseMode::Perlin;
            settings.width = settings.height = 2;
            settings.frequency = 1;
            const std::array<std::uint32_t, 3> seeds = { 0, 42, UINT32_MAX };
            const std::array<std::array<float, 3>, 3> references = {{
                { 0.609854681f, 0.551854593f, 0.336715817f },
                { 0.575054628f, 0.646833864f, 0.638267339f },
                { 0.464348824f, 0.426122307f, 0.499219811f },
            }};
            const std::array<Vector2, 3> coordinates = { Vector2(0.25f, 0.75f),
                Vector2(-0.25f, -0.75f), Vector2(-1.25f, 2.5f) };
            for (std::size_t seed = 0; seed < seeds.size(); ++seed)
                for (std::size_t point = 0; point < coordinates.size(); ++point)
                {
                    settings.seed = seeds[seed];
                    settings.offsetX = coordinates[point].x;
                    settings.offsetZ = coordinates[point].y;
                    if (!checks.expect(generateTerrainNoise(settings, repeated), "reference Perlin generation")
                        || !checks.close(repeated.samples()[0], references[seed][point], "Perlin independent reference including negative cells"))
                        return false;
                }
            settings.offsetX = -2;
            settings.offsetZ = -1;
            if (!checks.expect(generateTerrainNoise(settings, repeated)
                && std::all_of(repeated.samples().begin(), repeated.samples().end(), [](const float value) { return value == 0.5f; }),
                "Perlin lattice points have neutral normalized height"))
                return false;
            // A quintic gradient field must remain continuous on both sides of a negative cell boundary.
            settings.offsetX = -1.0001f;
            settings.offsetZ = 0.375f;
            if (!generateTerrainNoise(settings, repeated))
                return false;
            const float beforeBoundary = repeated.samples()[0];
            settings.offsetX = -0.9999f;
            if (!checks.expect(generateTerrainNoise(settings, repeated)
                && std::abs(repeated.samples()[0] - beforeBoundary) < 0.0002f, "negative lattice boundary continuity"))
                return false;

            settings = {};
            settings.seed = 17;
            settings.width = 17;
            settings.height = 9;
            settings.frequency = 3.5f;
            settings.offsetX = 0.25f;
            settings.offsetZ = -0.75f;
            settings.octaves = 4;
            if (!generateTerrainNoise(settings, fbm))
                return false;
            std::vector<double> reference(fbm.samples().size(), 0.0);
            double amplitude = 1;
            double weight = 0;
            TerrainNoiseSettings octave = settings;
            octave.mode = TerrainNoiseMode::Perlin;
            for (std::uint32_t index = 0; index < settings.octaves; ++index)
            {
                if (!generateTerrainNoise(octave, repeated))
                    return false;
                for (std::size_t sample = 0; sample < reference.size(); ++sample)
                    reference[sample] += amplitude * repeated.samples()[sample];
                weight += amplitude;
                amplitude *= settings.persistence;
                octave.frequency *= settings.lacunarity;
                octave.offsetX *= settings.lacunarity;
                octave.offsetZ *= settings.lacunarity;
            }
            bool matches = true;
            for (std::size_t sample = 0; sample < reference.size(); ++sample)
                matches = matches && std::abs(fbm.samples()[sample] - reference[sample] / weight) < 0.000001;
            if (!checks.expect(matches, "FBM equals independently weighted Perlin octave maps"))
                return false;
            TerrainNoiseSettings dense = settings;
            dense.width = 33;
            dense.height = 17;
            if (!generateTerrainNoise(dense, repeated))
                return false;
            matches = true;
            for (std::uint32_t z = 0; z < settings.height; ++z)
                for (std::uint32_t x = 0; x < settings.width; ++x)
                    matches = matches && fbm.samples()[static_cast<std::size_t>(z) * settings.width + x]
                        == repeated.samples()[static_cast<std::size_t>(z * 2) * dense.width + x * 2];
            if (!checks.expect(matches, "shared coordinates are invariant under grid resolution"))
                return false;
            TerrainNoiseSettings neighbor = settings;
            neighbor.offsetX += settings.frequency;
            if (!generateTerrainNoise(neighbor, repeated))
                return false;
            matches = true;
            for (std::uint32_t z = 0; z < settings.height; ++z)
                matches = matches && fbm.samples()[static_cast<std::size_t>(z) * settings.width + settings.width - 1]
                    == repeated.samples()[static_cast<std::size_t>(z) * settings.width];
            if (!checks.expect(matches, "adjacent coordinate domains agree without per-map min/max normalization"))
                return false;
            std::array<Heightmap, 4> workerMaps;
            std::array<bool, 4> workerSuccess{};
            JobSystem::instance().parallelFor(workerMaps.size(), [&](const std::size_t index)
            {
                workerSuccess[index] = generateTerrainNoise(settings, workerMaps[index]);
            });
            if (!checks.expect(std::all_of(workerSuccess.begin(), workerSuccess.end(), [](const bool value) { return value; })
                && std::all_of(workerMaps.begin(), workerMaps.end(), [&](const Heightmap& map) { return sameHeights(map, fbm); }),
                "parallel independent noise generation has no shared RNG state"))
                return false;

            const Heightmap previous = repeated;
            const auto rejected = [&](const TerrainNoiseSettings& invalid)
            {
                return checks.expect(!generateTerrainNoise(invalid, repeated) && sameHeights(repeated, previous),
                    "invalid noise settings preserve previous heightmap");
            };
            using Field = float TerrainNoiseSettings::*;
            const std::array<Field, 5> floatFields = { &TerrainNoiseSettings::frequency, &TerrainNoiseSettings::offsetX,
                &TerrainNoiseSettings::offsetZ, &TerrainNoiseSettings::persistence, &TerrainNoiseSettings::lacunarity };
            for (const auto field : floatFields)
                for (const float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(), -100001.0f, 100001.0f })
                {
                    auto invalidSettings = settings;
                    invalidSettings.*field = invalid;
                    if (!rejected(invalidSettings))
                        return false;
                }
            for (const auto field : { &TerrainNoiseSettings::width, &TerrainNoiseSettings::height })
                for (const auto invalid : { 0u, 1u, Heightmap::MAX_DIMENSION + 1, UINT32_MAX })
                {
                    auto invalidSettings = settings;
                    invalidSettings.*field = invalid;
                    if (!rejected(invalidSettings))
                        return false;
                }
            for (const auto invalid : { 0u, 13u, UINT32_MAX })
            {
                auto invalidSettings = settings;
                invalidSettings.octaves = invalid;
                if (!rejected(invalidSettings))
                    return false;
            }
            auto invalidSettings = settings;
            invalidSettings.mode = static_cast<TerrainNoiseMode>(255);
            if (!rejected(invalidSettings))
                return false;
            const std::array<std::pair<Field, float>, 11> invalidBounds = {{
                { &TerrainNoiseSettings::frequency, 0.0f }, { &TerrainNoiseSettings::frequency, 1024.01f },
                { &TerrainNoiseSettings::frequency, 0.00099f },
                { &TerrainNoiseSettings::persistence, -0.001f }, { &TerrainNoiseSettings::persistence, 1.001f },
                { &TerrainNoiseSettings::lacunarity, 0.999f }, { &TerrainNoiseSettings::lacunarity, 4.001f },
                { &TerrainNoiseSettings::offsetX, -100001.0f }, { &TerrainNoiseSettings::offsetX, 100001.0f },
                { &TerrainNoiseSettings::offsetZ, -100001.0f }, { &TerrainNoiseSettings::offsetZ, 100001.0f },
            }};
            for (const auto& [field, invalid] : invalidBounds)
            {
                invalidSettings = settings;
                invalidSettings.*field = invalid;
                if (!rejected(invalidSettings))
                    return false;
            }
            settings = { TerrainNoiseMode::FBM, UINT32_MAX, 9, 7, 1024, -100000, 100000, 12, 1, 4 };
            if (!checks.expect(generateTerrainNoise(settings, repeated) && normalizedHeights(repeated),
                "maximum octave coordinate range stays finite and defined"))
                return false;
            settings.frequency = 0.001f;
            settings.offsetX = settings.offsetZ = 0;
            settings.persistence = 0;
            settings.lacunarity = 1;
            if (!checks.expect(generateTerrainNoise(settings, repeated) && normalizedHeights(repeated),
                "minimum noise parameter boundaries"))
                return false;
            settings = {};
            settings.width = settings.height = Heightmap::MAX_DIMENSION;
            settings.octaves = 2;
            return checks.expect(generateTerrainNoise(settings, repeated) && normalizedHeights(repeated)
                && repeated.samples().size() == 1050625, "maximum 1025x1025 generated heightmap");
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
            if (!checks.expect(!terrain->deserializePayload(7, payload), "reject unknown payload version"))
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

        bool validateNoisePersistence(Checks& checks, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "Noise source");
            auto* object = scene.createGameObject("Generated terrain");
            auto* terrain = object != nullptr ? object->addComponent<TerrainComponent>() : nullptr;
            TerrainNoiseSettings settings{ TerrainNoiseMode::FBM, UINT32_MAX, 17, 9, 3.25f, -0.75f, 0.125f, 4, 0.625f, 2.5f };
            const TerrainMeshSettings mesh{ 7, 11, 6, -2, 2, 3 };
            if (!checks.expect(terrain != nullptr && terrain->generateNoise(settings, mesh)
                && terrain->getNoiseSettings() == settings, "component applies and retains noise recipe"))
                return false;
            const Heightmap generated = terrain->getHeightmap();
            const auto model = terrain->getModel();
            auto invalid = settings;
            invalid.persistence = std::numeric_limits<float>::quiet_NaN();
            auto badMesh = mesh;
            badMesh.sizeX = 0;
            if (!checks.expect(!terrain->generateNoise(invalid, mesh) && !terrain->generateNoise(settings, badMesh)
                && terrain->getModel() == model && terrain->getNoiseSettings() == settings
                && sameHeights(terrain->getHeightmap(), generated), "failed noise generation preserves recipe and geometry"))
                return false;
            const auto cpuModel = ModelManager::instance().get(model);
            if (!checks.expect(cpuModel != nullptr && cpuModel->meshes[0].vertices.size() == generated.samples().size(),
                "noise generation feeds existing mesh builder"))
                return false;
            bool matches = true;
            for (std::size_t sample = 0; sample < generated.samples().size(); ++sample)
                matches = matches && std::abs(cpuModel->meshes[0].vertices[sample].position.y
                    - (mesh.heightOffset + generated.samples()[sample] * mesh.heightScale)) < 0.00001f;
            if (!checks.expect(matches, "noise height samples map to every mesh vertex"))
                return false;
            std::vector<std::uint8_t> payload;
            TerrainComponent restored;
            if (!checks.expect(terrain->serializePayload(payload) && terrain->getPayloadVersion() == 6
                && restored.deserializePayload(2, payload) && restored.getNoiseSettings() == settings
                && sameHeights(restored.getHeightmap(), generated), "version 2 noise recipe and samples roundtrip"))
                return false;
            if (!checks.expect(restored.generateNoise(restored.getNoiseSettings(), restored.getSettings())
                && sameHeights(restored.getHeightmap(), generated), "loaded recipe regenerates exact heights with aliased arguments"))
                return false;

            const auto previous = restored.getModel();
            for (const std::uint32_t version : { 0u, 7u })
                if (!checks.expect(!restored.deserializePayload(version, payload) && restored.getModel() == previous,
                    "reject unsupported terrain payload versions"))
                    return false;
            flatbuffers::FlatBufferBuilder legacy;
            const std::array<float, 4> flat = { 0.125f, 0.25f, 0.5f, 0.75f };
            const auto legacyRoot = Serialization::CreateTerrainComponentPayload(legacy, 2, 2,
                legacy.CreateVector(flat.data(), flat.size()), 12, 14, 8, -3, 5, 6, false);
            Serialization::FinishTerrainComponentPayloadBuffer(legacy, legacyRoot);
            if (!checks.expect(!restored.deserializePayload(2, { legacy.GetBufferPointer(), legacy.GetSize() })
                && restored.getModel() == previous && restored.getNoiseSettings() == settings,
                "version 2 missing recipe is rejected transactionally"))
                return false;
            for (const auto invalidMode : { 0u, 255u })
            {
                flatbuffers::FlatBufferBuilder malformed;
                const auto noise = Serialization::CreateTerrainNoiseSettings(malformed, static_cast<std::uint8_t>(invalidMode),
                    7, invalidMode == 0 ? 1u : 17u, 9);
                const auto root = Serialization::CreateTerrainComponentPayload(malformed, 2, 2,
                    malformed.CreateVector(flat.data(), flat.size()), 12, 14, 8, -3, 5, 6, false, noise);
                Serialization::FinishTerrainComponentPayloadBuffer(malformed, root);
                if (!checks.expect(!restored.deserializePayload(2, { malformed.GetBufferPointer(), malformed.GetSize() })
                    && restored.getModel() == previous && restored.getNoiseSettings() == settings
                    && sameHeights(restored.getHeightmap(), generated), "invalid serialized recipe preserves all component state"))
                    return false;
            }
            if (!checks.expect(restored.deserializePayload(1, { legacy.GetBufferPointer(), legacy.GetSize() })
                && restored.getNoiseSettings() == TerrainNoiseSettings{} && !restored.getCastShadows()
                && restored.getSettings().sizeX == 12 && restored.getHeightmap().samples()[3] == 0.75f,
                "legacy version 1 loads embedded heights unchanged with default recipe"))
                return false;
            std::vector<std::uint8_t> upgraded;
            TerrainComponent migrated;
            if (!checks.expect(restored.serializePayload(upgraded) && migrated.deserializePayload(3, upgraded)
                && sameHeights(migrated.getHeightmap(), restored.getHeightmap())
                && migrated.getNoiseSettings() == TerrainNoiseSettings{}, "legacy payload upgrades to current version"))
                return false;

            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"noise.scene", scene), "save noise terrain scene"))
                return false;
            Scene loaded(ObjectGUID::generate(), "Loaded noise");
            if (!checks.expect(Serialization::SceneSerializer{}.load(directory / L"noise.scene", loaded), "reload noise terrain scene"))
                return false;
            const auto* loadedObject = loaded.find(object->getGUID());
            const auto* reloaded = loadedObject != nullptr ? loadedObject->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(reloaded != nullptr && reloaded->getNoiseSettings() == settings
                && sameHeights(reloaded->getHeightmap(), generated), "scene preserves generated samples and complete recipe"))
                return false;
            Scene clonedScene(ObjectGUID::generate(), "Noise clone");
            const SceneClonePair pair{ scene, clonedScene };
            if (!checks.expect(cloneScenes(std::span(&pair, 1)), "clone noise scene"))
                return false;
            const auto* clonedObject = clonedScene.find(object->getGUID());
            const auto* cloned = clonedObject != nullptr ? clonedObject->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(cloned != nullptr && cloned->getNoiseSettings() == settings
                && cloned->getModel() != terrain->getModel() && sameHeights(cloned->getHeightmap(), generated),
                "Play clone preserves recipe with independent terrain"))
                return false;
            Prefab prefab;
            Prefab loadedPrefab;
            if (!checks.expect(prefab.capture(*object) && Serialization::PrefabSerializer{}.save(directory / L"noise.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"noise.prefab", loadedPrefab), "save and reload noise prefab"))
                return false;
            Scene instances(ObjectGUID::generate(), "Noise instances");
            auto* instance = loadedPrefab.instantiate(instances);
            const auto* instanceTerrain = instance != nullptr ? instance->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(instanceTerrain != nullptr && instanceTerrain->getNoiseSettings() == settings
                && sameHeights(instanceTerrain->getHeightmap(), generated), "prefab retains noise recipe and heights"))
                return false;
            Heightmap flatMap;
            if (!checks.expect(flatMap.createFlat(3, 2, 0.25f) && terrain->setTerrain(flatMap, mesh)
                && terrain->getNoiseSettings() == settings && sameHeights(cloned->getHeightmap(), generated),
                "import/flat replacement retains last noise recipe without modifying clones"))
                return false;
            return checks.expect(terrain->serializePayload(payload) && restored.deserializePayload(2, payload)
                && sameHeights(restored.getHeightmap(), flatMap) && restored.getNoiseSettings() == settings,
                "saved samples remain authoritative even when they differ from last noise recipe");
        }

        bool validateChunks(Checks& checks, const std::filesystem::path& directory)
        {
            Scene scene(ObjectGUID::generate(), "Chunked terrain");
            auto* object = scene.createGameObject("Chunks");
            auto* terrain = object->addComponent<TerrainComponent>();
            TerrainNoiseSettings noise;
            noise.width = 41;
            noise.height = 36;
            noise.seed = 198;
            const TerrainMeshSettings meshSettings{ 7.3f, 4.7f, 3, -1, 3, 5 };
            TerrainMaterialSettings material;
            material.enabled = true;
            if (!checks.expect(terrain->generateNoise(noise, meshSettings)
                && terrain->setMaterialSettings(material), "chunked material/noise fixture"))
                return false;
            ModelResource reference;
            if (!checks.expect(buildTerrainModel(terrain->getHeightmap(), meshSettings, reference)
                && applyTerrainLayerWeights(terrain->getHeightmap(), material, reference), "whole-grid chunk oracle"))
                return false;
            for (const std::uint32_t edge : { 16u, 17u, 32u, 64u, 1024u })
            {
                if (!checks.expect(terrain->setChunkSettings({ edge }), "apply valid cell edge including non-power-of-two"))
                    return false;
                const auto model = ModelManager::instance().get(terrain->getModel());
                const auto chunks = terrain->getChunks();
                const auto columns = (noise.width - 2) / edge + 1;
                const auto rows = (noise.height - 2) / edge + 1;
                if (!checks.expect(model && chunks.size() == columns * rows && model->meshes.size() == chunks.size()
                    && model->nodes.size() == 1 && model->nodes[0].meshIndices.size() == chunks.size(),
                    "chunk metadata, model meshes and node references agree"))
                    return false;
                std::vector<std::uint32_t> coverage(static_cast<std::size_t>(noise.width - 1) * (noise.height - 1));
                for (std::size_t index = 0; index < chunks.size(); ++index)
                {
                    const auto& chunk = chunks[index];
                    const auto& mesh = model->meshes[index];
                    if (!checks.expect(chunk.meshIndex == index && chunk.column == index % columns && chunk.row == index / columns
                        && chunk.sampleX == chunk.column * edge && chunk.sampleZ == chunk.row * edge
                        && chunk.cellsX == std::min(edge, noise.width - 1 - chunk.sampleX)
                        && chunk.cellsZ == std::min(edge, noise.height - 1 - chunk.sampleZ)
                        && mesh.vertices.size() == static_cast<std::size_t>(chunk.cellsX + 1) * (chunk.cellsZ + 1)
                        && mesh.indices.size() == static_cast<std::size_t>(chunk.cellsX) * chunk.cellsZ * 6
                        && mesh.subMeshes.size() == 1 && mesh.subMeshes[0].indexCount == mesh.indices.size(),
                        "row-major chunk range and partial-edge counts"))
                        return false;
                    for (std::uint32_t z = 0; z <= chunk.cellsZ; ++z)
                        for (std::uint32_t x = 0; x <= chunk.cellsX; ++x)
                        {
                            const auto& actual = mesh.vertices[static_cast<std::size_t>(z) * (chunk.cellsX + 1) + x];
                            const auto& expected = reference.meshes[0].vertices[
                                static_cast<std::size_t>(z + chunk.sampleZ) * noise.width + x + chunk.sampleX];
                            if (!checks.expect(actual.position == expected.position && actual.normal == expected.normal
                                && actual.tangent == expected.tangent && actual.bitangent == expected.bitangent
                                && actual.texCoord == expected.texCoord && actual.color == expected.color,
                                "every chunk vertex exactly preserves whole-grid geometry, smooth frame, UV and layers")
                                || !checks.expect(mesh.boundingBox.Contains(actual.position) != DirectX::DISJOINT
                                    && chunk.bounds.Contains(actual.position) != DirectX::DISJOINT,
                                    "chunk-local bounds contain every vertex"))
                                return false;
                        }
                    for (std::uint32_t z = 0; z < chunk.cellsZ; ++z)
                        for (std::uint32_t x = 0; x < chunk.cellsX; ++x)
                        {
                            ++coverage[static_cast<std::size_t>(z + chunk.sampleZ) * (noise.width - 1) + x + chunk.sampleX];
                            const auto a = z * (chunk.cellsX + 1) + x;
                            const std::array expected{ a, a + chunk.cellsX + 1, a + 1, a + 1,
                                a + chunk.cellsX + 1, a + chunk.cellsX + 2 };
                            const auto start = (static_cast<std::size_t>(z) * chunk.cellsX + x) * 6;
                            if (!checks.expect(std::equal(expected.begin(), expected.end(), mesh.indices.begin() + start),
                                "chunk triangles preserve source winding and diagonal"))
                                return false;
                        }
                }
                if (!checks.expect(std::ranges::all_of(coverage, [](auto count) { return count == 1; }),
                    "every source cell belongs to exactly one chunk without holes or duplicate triangles"))
                    return false;
            }
            if (!checks.expect(terrain->setChunkSettings({ 16 }), "restore multiple partial chunks for persistence"))
                return false;
            const auto previous = terrain->getModel();
            for (const std::uint32_t invalid : { 0u, 15u, 1025u, UINT32_MAX })
                if (!checks.expect(!terrain->setChunkSettings({ invalid }) && terrain->getModel() == previous
                    && terrain->getChunks().size() == 9 && terrain->getChunkSettings().cellsPerChunk == 16,
                    "invalid partition settings preserve applied resources and chunks"))
                    return false;
            std::vector<std::uint8_t> payload;
            TerrainComponent restored;
            if (!checks.expect(terrain->serializePayload(payload) && restored.deserializePayload(4, payload)
                && restored.getChunkSettings() == terrain->getChunkSettings() && restored.getChunks().size() == 9
                && restored.getNoiseSettings() == noise && restored.getMaterialSettings().enabled,
                "version 4 embeds chunk settings with heights, recipe and material"))
                return false;
            for (const auto version : { 1u, 2u, 3u })
                if (!checks.expect(restored.deserializePayload(version, payload)
                    && restored.getChunkSettings().cellsPerChunk == 1024 && restored.getChunks().size() == 1,
                    "legacy versions retain monolithic partition and unchanged surface"))
                    return false;
            for (const std::uint32_t invalid : { 0u, 15u, 1025u, UINT32_MAX })
            {
                flatbuffers::FlatBufferBuilder malformed;
                flatbuffers::Offset<Serialization::TerrainMaterialData> surface;
                if (!Serialization::writeTerrainMaterial(malformed, material, surface))
                    return false;
                const auto recipe = Serialization::CreateTerrainNoiseSettings(malformed);
                const auto samples = terrain->getHeightmap().samples();
                const auto root = Serialization::CreateTerrainComponentPayload(malformed, noise.width, noise.height,
                    malformed.CreateVector(samples.data(), samples.size()), 7.3f, 4.7f, 3, -1, 3, 5, true, recipe, surface, invalid);
                Serialization::FinishTerrainComponentPayloadBuffer(malformed, root);
                if (!checks.expect(!terrain->deserializePayload(4, { malformed.GetBufferPointer(), malformed.GetSize() })
                    && terrain->getModel() == previous && terrain->getChunks().size() == 9,
                    "malformed v4 chunk settings reject transactionally"))
                    return false;
            }
            Scene loaded(ObjectGUID::generate(), "Chunk scene loaded");
            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"chunks.scene", scene)
                && Serialization::SceneSerializer{}.load(directory / L"chunks.scene", loaded), "chunk scene roundtrip"))
                return false;
            const auto* loadedTerrain = loaded.find(object->getGUID())->getComponent<TerrainComponent>();
            if (!checks.expect(loadedTerrain && loadedTerrain->getChunks().size() == 9
                && loadedTerrain->getChunkSettings().cellsPerChunk == 16 && loadedTerrain->getModel() != previous,
                "loaded chunks own independent geometry"))
                return false;
            Prefab prefab, loadedPrefab;
            Scene instances(ObjectGUID::generate(), "Chunk prefab instance");
            if (!checks.expect(prefab.capture(*object)
                && Serialization::PrefabSerializer{}.save(directory / L"chunks.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"chunks.prefab", loadedPrefab), "chunk prefab roundtrip"))
                return false;
            const auto* instance = loadedPrefab.instantiate(instances);
            if (!checks.expect(instance && instance->getComponent<TerrainComponent>()->getChunks().size() == 9,
                "prefab rebuilds complete chunk grid"))
                return false;
            Scene clone(ObjectGUID::generate(), "Chunk clone");
            const SceneClonePair pair{ scene, clone };
            if (!checks.expect(cloneScenes(std::span(&pair, 1))
                && clone.find(object->getGUID())->getComponent<TerrainComponent>()->getChunks().size() == 9,
                "Play clone preserves chunk layout"))
                return false;
            auto& queue = ModelRenderSubmissionQueue::instance();
            queue.clear();
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            queue.consume(submissions);
            if (!checks.expect(submissions.size() == 1 && submissions[0].modelSnapshot->meshes.size() == 9
                && terrain->setChunkSettings({ 32 }) && terrain->getChunks().size() == 4,
                "one immutable submission retains every chunk across repartition"))
                return false;
            scene.clear();
            if (!checks.expect(submissions[0].modelSnapshot->meshes.size() == 9
                && ModelManager::instance().get(previous) == nullptr, "queued chunk geometry survives owner destruction"))
                return false;
            Heightmap maximum;
            ModelResource large;
            std::vector<TerrainChunk> chunks;
            if (!checks.expect(maximum.createFlat(1025, 1025) && buildTerrainModel(maximum, {}, large)
                && partitionTerrainModel(large, 1025, 1025, { 16 }, chunks) && chunks.size() == 4096,
                "maximum grid has bounded 4096 resident chunks"))
                return false;
            std::size_t indices = 0;
            for (const auto& mesh : large.meshes)
                indices += mesh.indices.size();
            return checks.expect(indices == 6291456 && large.meshes.back().vertices.size() == 289,
                "maximum partition preserves every triangle with bounded chunk vertex count");
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
            io.DisplaySize = { 1600, 3200 };
            io.DeltaTime = 1.0f / 60.0f;
            unsigned char* pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            io.Fonts->SetTexID(static_cast<ImTextureID>(1));
            TerrainComponent terrain;
            TerrainComponent noiseTerrain;
            TerrainComponent* selected = &terrain;
            ImGuiID rebuildButton = 0, applyChunks = 0, enableLod = 0, applyLod = 0;
            ImGuiID noiseButton = 0, gpuNoiseButton = 0, cancelButton = 0;
            ImGuiID erosionHeader = 0, erosionButton = 0, resetErosion = 0;
            ImGuiID materialHeader = 0, enableLayers = 0, applyLayers = 0, resetLayers = 0;
            const auto frame = [&]
            {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({ 10, 10 }, ImGuiCond_Always);
                ImGui::SetNextWindowSize({ 1500, 3100 }, ImGuiCond_Always);
                ImGui::Begin("Terrain Inspector Validation", nullptr, ImGuiWindowFlags_NoSavedSettings);
                rebuildButton = ImGui::GetID("Apply / Rebuild");
                applyChunks = ImGui::GetID("Apply Chunk Settings");
                enableLod = ImGui::GetID("Enable Terrain LOD");
                applyLod = ImGui::GetID("Apply LOD Settings");
                materialHeader = ImGui::GetID("Terrain Material Layers");
                enableLayers = ImGui::GetID("Enable Layered Material");
                applyLayers = ImGui::GetID("Apply Terrain Material");
                resetLayers = ImGui::GetID("Reset Layer Draft");
                noiseButton = ImGui::GetID("Generate Noise");
                gpuNoiseButton = ImGui::GetID("Generate Noise (GPU)");
                cancelButton = ImGui::GetID("Cancel Terrain Generation");
                erosionHeader = ImGui::GetID("Hydraulic Erosion");
                ImGui::PushOverrideID(erosionHeader);
                erosionButton = ImGui::GetID("Apply Hydraulic Erosion");
                resetErosion = ImGui::GetID("Reset Erosion Draft");
                ImGui::PopID();
                selected->drawImGui();
                ImGui::End();
                ImGui::Render();
            };
            const auto complete = [&]
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (selected->getGenerationStatus().pending() && std::chrono::steady_clock::now() < deadline)
                {
                    TerrainGenerationQueue::instance().applyCompleted();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return checks.expect(selected->getGenerationStatus().stage == TerrainGenerationStage::Applied,
                    "Inspector background request applies at a frame barrier");
            };
            frame();
            ImGui::ActivateItemByID(rebuildButton); frame();
            if (!checks.expect(!terrain.getModel().isValid() && terrain.getGenerationStatus().pending(),
                "Inspector Apply queues work without generating on the UI thread") || !complete())
                return false;
            if (!checks.expect(terrain.getModel().isValid() && terrain.getHeightmap().width() == 65,
                "Inspector Apply/Rebuild button generates terrain"))
                return false;
            const auto beforeChunks = terrain.getModel();
            ImGui::ActivateItemByID(applyChunks); frame();
            if (!complete())
                return false;
            if (!checks.expect(terrain.getModel() != beforeChunks && terrain.getChunks().size() == 1
                && terrain.getChunkSettings().cellsPerChunk == 64, "Inspector applies chunk settings and publishes metadata"))
                return false;
            ImGui::ActivateItemByID(enableLod); frame();
            if (!checks.expect(!terrain.getLodSettings().enabled, "Inspector LOD edits remain drafts until Apply"))
                return false;
            ImGui::ActivateItemByID(applyLod); frame();
            ImGui::ActivateItemByID(enableLod); frame();
            if (!complete())
                return false;
            if (!checks.expect(terrain.getLodSettings().enabled && terrain.getLodSnapshot()->chunks[0].levelCount > 1,
                "Inspector Apply LOD Settings publishes enabled immutable levels"))
                return false;
            ImGui::ActivateItemByID(applyLod); frame();
            if (!complete())
                return false;
            if (!checks.expect(!terrain.getLodSettings().enabled && terrain.getLodSnapshot()->chunks[0].levelCount == 1,
                "Inspector can disable LOD and restore full-resolution-only indices"))
                return false;
            selected = &noiseTerrain;
            frame();
            ImGui::ActivateItemByID(noiseButton); frame();
            if (!complete())
                return false;
            Heightmap expected;
            if (!checks.expect(generateTerrainNoise({}, expected) && noiseTerrain.getModel().isValid()
                && sameHeights(noiseTerrain.getHeightmap(), expected), "Inspector Generate Noise applies default FBM recipe"))
                return false;
            ImGui::ActivateItemByID(gpuNoiseButton); frame();
            if (!checks.expect(noiseTerrain.getGenerationStatus().pending(), "Inspector GPU noise queues explicitly") || !complete())
                return false;
            if (!checks.expect(TerrainGpuNoise::instance().generate({}, expected) && sameHeights(noiseTerrain.getHeightmap(), expected),
                "Inspector GPU noise applies actual Compute readback samples"))
                return false;
            ImGui::ActivateItemByID(materialHeader); frame();
            ImGui::ActivateItemByID(enableLayers); frame();
            if (!checks.expect(!noiseTerrain.getMaterialSettings().enabled, "Inspector material edits are staged until Apply"))
                return false;
            ImGui::ActivateItemByID(applyLayers); frame();
            if (!complete())
                return false;
            if (!checks.expect(noiseTerrain.getMaterialSettings().enabled && noiseTerrain.getMaterial().isValid(),
                "Inspector Apply Terrain Material creates layered surface"))
                return false;
            ImGui::ActivateItemByID(resetLayers); frame();
            if (!checks.expect(noiseTerrain.getMaterialSettings().enabled, "Inspector Reset Layer Draft does not alter applied material"))
                return false;
            ImGui::ActivateItemByID(applyLayers); frame();
            if (!complete())
                return false;
            if (!checks.expect(!noiseTerrain.getMaterialSettings().enabled && !noiseTerrain.getMaterial().isValid(),
                "Inspector applying disabled draft restores default material"))
                return false;
            TerrainErosionSettings erosion;
            erosion.seed = 17;
            erosion.droplets = 500;
            if (!checks.expect(noiseTerrain.applyErosion(erosion), "nondefault erosion Inspector fixture"))
                return false;
            Heightmap eroded;
            if (!checks.expect(erodeTerrain(noiseTerrain.getHeightmap(), erosion, eroded), "Inspector erosion oracle"))
                return false;
            frame();
            ImGui::ActivateItemByID(erosionHeader); frame();
            const auto oldErosionModel = noiseTerrain.getModel();
            ImGui::ActivateItemByID(erosionButton); frame();
            if (!checks.expect(noiseTerrain.getGenerationStatus().pending() && noiseTerrain.getModel() == oldErosionModel,
                "Inspector erosion queues rather than blocking/replacing live geometry"))
                return false;
            ImGui::ActivateItemByID(resetErosion); frame();
            if (!complete() || !checks.expect(noiseTerrain.getErosionSettings() == erosion
                && sameHeights(noiseTerrain.getHeightmap(), eroded), "Inspector erosion applies captured recipe and heights"))
                return false;
            if (!checks.expect(erodeTerrain(eroded, {}, eroded), "cumulative default erosion Inspector oracle"))
                return false;
            ImGui::ActivateItemByID(erosionButton); frame();
            if (!complete() || !checks.expect(noiseTerrain.getErosionSettings() == TerrainErosionSettings{}
                && sameHeights(noiseTerrain.getHeightmap(), eroded), "newer erosion draft reset survives older background completion"))
                return false;
            const auto previous = noiseTerrain.getModel();
            ImGui::ActivateItemByID(noiseButton); frame();
            ImGui::ActivateItemByID(cancelButton); frame();
            TerrainGenerationQueue::instance().applyCompleted();
            return checks.expect(noiseTerrain.getGenerationStatus().stage == TerrainGenerationStage::Cancelled
                && noiseTerrain.getModel() == previous, "Inspector cancel prevents ready or active background work from applying");
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
            TerrainNoiseSettings recipe;
            recipe.width = recipe.height = 33;
            recipe.seed = 42;
            const TerrainMeshSettings settings{ 4, 4, 2, 0, 1, 1 };
            if (!checks.expect(terrain != nullptr && generateTerrainNoise(recipe, heights)
                && terrain->generateNoise(recipe, settings),
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
            if (!checks.expect(terrain->generateNoise(recipe, settings), "new unuploaded terrain"))
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
            recipe.mode = TerrainNoiseMode::Perlin;
            if (!checks.expect(forward != nullptr && forward->generateNoise(recipe, settings), "forward Perlin terrain fixture"))
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

        bool sameSurface(const TerrainMaterialSettings& left, const TerrainMaterialSettings& right)
        {
            flatbuffers::FlatBufferBuilder a;
            flatbuffers::FlatBufferBuilder b;
            flatbuffers::Offset<Serialization::TerrainMaterialData> aRoot, bRoot;
            if (!Serialization::writeTerrainMaterial(a, left, aRoot) || !Serialization::writeTerrainMaterial(b, right, bRoot))
                return false;
            a.Finish(aRoot);
            b.Finish(bRoot);
            return a.GetSize() == b.GetSize() && std::memcmp(a.GetBufferPointer(), b.GetBufferPointer(), a.GetSize()) == 0;
        }

        bool validateLayerWeights(Checks& checks)
        {
            TerrainMaterialSettings settings;
            settings.enabled = true;
            if (!checks.expect(validateTerrainMaterial(settings), "default soil grass rock snow settings"))
                return false;
            for (auto& layer : settings.layers)
                layer = TerrainLayer{};
            settings.layers[1].strength = 0.5f;
            settings.layers[2].strength = 0.25f;
            settings.layers[3].strength = 0.5f;
            const auto weights = terrainLayerWeights(settings, 0.5f, 45);
            if (!checks.close(weights.x, 0.1875f, "ordered base remainder")
                || !checks.close(weights.y, 0.1875f, "ordered grass remainder")
                || !checks.close(weights.z, 0.125f, "ordered rock remainder")
                || !checks.close(weights.w, 0.5f, "last layer priority"))
                return false;
            settings.layerCount = 2;
            settings.layers[1].minimumHeight = 0.5f;
            settings.layers[1].heightBlend = 0.25f;
            settings.layers[1].minimumSlope = 40;
            settings.layers[1].slopeBlend = 20;
            settings.layers[1].strength = 1;
            const auto soft = terrainLayerWeights(settings, 0.375f, 30);
            if (!checks.close(soft.x, 0.75f, "combined height and slope soft remainder")
                || !checks.close(soft.y, 0.25f, "height and slope smoothstep product")
                || !checks.close(soft.z + soft.w, 0, "inactive layer weights zero"))
                return false;
            settings.layers[1].heightBlend = settings.layers[1].slopeBlend = 0;
            if (!checks.close(terrainLayerWeights(settings, 0.5f, 40).y, 1, "zero falloff includes exact band edge")
                || !checks.close(terrainLayerWeights(settings, 0.499f, 40).x, 1, "uncovered regions use base layer"))
                return false;
            settings = {};
            bool normalized = true;
            for (int h = 0; h <= 100; ++h)
                for (int s = 0; s <= 90; ++s)
                {
                    const auto value = terrainLayerWeights(settings, static_cast<float>(h) / 100, static_cast<float>(s));
                    normalized = normalized && value.x >= 0 && value.y >= 0 && value.z >= 0 && value.w >= 0
                        && std::abs(value.x + value.y + value.z + value.w - 1) < 0.000002f;
                }
            if (!checks.expect(normalized, "9191 height-slope samples have nonnegative normalized weights"))
                return false;
            const std::array fields = {
                &TerrainLayer::metallic, &TerrainLayer::roughness, &TerrainLayer::normalScale, &TerrainLayer::uvRepeat,
                &TerrainLayer::minimumHeight, &TerrainLayer::maximumHeight, &TerrainLayer::heightBlend,
                &TerrainLayer::minimumSlope, &TerrainLayer::maximumSlope, &TerrainLayer::slopeBlend, &TerrainLayer::strength,
            };
            for (const auto field : fields)
                for (const float invalid : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f })
                {
                    auto bad = settings;
                    bad.layerCount = 1;
                    bad.layers[3].*field = invalid;
                    if (!checks.expect(!validateTerrainMaterial(bad), "invalid inactive layer field rejected"))
                        return false;
                }
            for (const std::uint32_t count : { 0u, 5u, UINT32_MAX })
            {
                auto bad = settings;
                bad.layerCount = count;
                if (!checks.expect(!validateTerrainMaterial(bad), "invalid layer count rejected"))
                    return false;
            }
            const std::array upperBounds = { 1.0f, 1.0f, 2.0f, 10000.0f, 1.0f, 1.0f, 1.0f, 90.0f, 90.0f, 90.0f, 1.0f };
            for (std::size_t index = 0; index < fields.size(); ++index)
            {
                auto bad = settings;
                bad.layers[0].*fields[index] = upperBounds[index] + 1;
                if (!checks.expect(!validateTerrainMaterial(bad), "layer numeric upper bound enforced"))
                    return false;
            }
            for (const Vector4 color : { Vector4(-0.1f, 1, 1, 1), Vector4(1, 1.1f, 1, 1),
                Vector4(1, 1, std::numeric_limits<float>::quiet_NaN(), 1), Vector4(1, 1, 1, 0.5f) })
            {
                auto bad = settings;
                bad.layers[0].color = color;
                if (!checks.expect(!validateTerrainMaterial(bad), "opaque linear layer color bounds"))
                    return false;
            }
            auto bad = settings;
            bad.layers[0].minimumHeight = 0.9f;
            bad.layers[0].maximumHeight = 0.2f;
            if (!checks.expect(!validateTerrainMaterial(bad), "inverted height range rejected"))
                return false;
            bad = settings;
            bad.layers[0].minimumSlope = 80;
            bad.layers[0].maximumSlope = 10;
            if (!checks.expect(!validateTerrainMaterial(bad), "inverted slope range rejected"))
                return false;
            bad = settings;
            bad.layers[0].normalPath = L"path-only.png";
            if (!checks.expect(!validateTerrainMaterial(bad), "path without GUID rejected"))
                return false;
            Heightmap heights;
            ModelResource model;
            const std::array<float, 4> ramp = { 0, 1, 0, 1 };
            settings.enabled = true;
            settings.layerCount = 2;
            settings.layers[1] = TerrainLayer{};
            settings.layers[1].minimumSlope = 40;
            settings.layers[1].heightBlend = 0;
            if (!checks.expect(heights.assign(2, 2, ramp) && buildTerrainModel(heights, { 4, 4, 4, 0, 1, 1 }, model)
                && applyTerrainLayerWeights(heights, settings, model), "slope layer fixture"))
                return false;
            for (const auto& vertex : model.meshes[0].vertices)
                if (!checks.close(vertex.color.y, 1, "45-degree local slope selects overlay"))
                    return false;
            if (!checks.expect(buildTerrainModel(heights, { 4, 4, 0, 0, 1, 1 }, model)
                && applyTerrainLayerWeights(heights, settings, model)
                && model.meshes[0].vertices[0].color == Vector4(1, 0, 0, 0), "zero height scale recalculates slope masks"))
                return false;
            settings.enabled = false;
            if (!checks.expect(applyTerrainLayerWeights(heights, settings, model)
                && model.meshes[0].vertices[0].color == Vector4::One, "disabled layers preserve standard white vertices"))
                return false;
            model.meshes[0].vertices.pop_back();
            return checks.expect(!applyTerrainLayerWeights(heights, settings, model), "mismatched layer geometry rejected");
        }

        bool writeLayerTextures(Checks& checks, const std::filesystem::path& directory, TerrainLayer& layer)
        {
            const std::array names = { L"layer-color.png", L"layer-normal.png", L"layer-mr.png" };
            const std::array references = { std::pair{ &layer.baseColorTexture, &layer.baseColorPath },
                std::pair{ &layer.normalTexture, &layer.normalPath }, std::pair{ &layer.metallicRoughnessTexture, &layer.metallicRoughnessPath } };
            for (std::size_t channel = 0; channel < names.size(); ++channel)
            {
                DirectX::ScratchImage image;
                if (!checks.expect(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 1)), "layer texture allocation"))
                    return false;
                const auto* pixels = image.GetImage(0, 0, 0);
                for (std::size_t row = 0; row < 16; ++row)
                    for (std::size_t column = 0; column < 16; ++column)
                    {
                        const std::array<std::uint8_t, 4> color = channel == 0
                            ? (column < 8 ? std::array<std::uint8_t, 4>{ 128, 128, 128, 255 }
                                : std::array<std::uint8_t, 4>{ 32, 64, 192, 255 })
                            : (channel == 1 ? std::array<std::uint8_t, 4>{ 204, 128, 230, 255 }
                                : std::array<std::uint8_t, 4>{ 23, 128, 204, 255 });
                        std::memcpy(pixels->pixels + row * pixels->rowPitch + column * 4, color.data(), 4);
                    }
                *references[channel].second = std::filesystem::absolute(directory / names[channel]).lexically_normal();
                if (!checks.expect(SUCCEEDED(DirectX::SaveToWICFile(*pixels, DirectX::WIC_FLAGS_NONE,
                    DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), references[channel].second->c_str())), "save layer texture"))
                    return false;
                *references[channel].first = TextureManager::instance().registerAssetPath(*references[channel].second);
                if (!checks.expect(references[channel].first->isValid(), "register layer texture GUID"))
                    return false;
            }
            return true;
        }

        bool validateLayerPersistence(Checks& checks, const std::filesystem::path& directory, const TerrainLayer& textured)
        {
            Scene scene(ObjectGUID::generate(), "Layered terrain");
            auto* object = scene.createGameObject("Layers");
            auto* terrain = object ? object->addComponent<TerrainComponent>() : nullptr;
            TerrainMaterialSettings settings;
            settings.enabled = true;
            settings.layers[2] = textured;
            settings.layers[2].minimumSlope = 35;
            settings.layers[2].metallic = 0.3f;
            settings.layers[2].roughness = 0.75f;
            settings.layers[2].normalScale = 0.6f;
            settings.layers[2].uvRepeat = 7;
            settings.layers[3].strength = 0.4f;
            if (!checks.expect(terrain && terrain->setMaterialSettings(settings)
                && terrain->getMaterial().isValid(), "apply layered component material"))
                return false;
            const auto material = MaterialManager::instance().get(terrain->getMaterial());
            if (!checks.expect(material && material->terrain && sameSurface(*material->terrain, settings),
                "immutable generated material contains complete layers"))
                return false;
            const auto originalModel = terrain->getModel();
            const auto originalMaterial = terrain->getMaterial();
            std::vector<std::uint8_t> payload;
            TerrainComponent restored;
            if (!checks.expect(terrain->serializePayload(payload) && restored.deserializePayload(3, payload)
                && sameSurface(restored.getMaterialSettings(), settings)
                && restored.getMaterial() != originalMaterial, "version 3 layered material roundtrip with independent ownership"))
                return false;
            auto bad = settings;
            bad.layers[3].uvRepeat = 0;
            if (!checks.expect(!terrain->setMaterialSettings(bad)
                && terrain->getModel() == originalModel && terrain->getMaterial() == originalMaterial
                && sameSurface(terrain->getMaterialSettings(), settings), "failed surface apply preserves all resources and settings"))
                return false;
            bad = settings;
            bad.layers[0].baseColorTexture = AssetGUID::generate();
            bad.layers[0].baseColorPath = textured.baseColorPath;
            if (!checks.expect(!terrain->setMaterialSettings(bad) && terrain->getMaterial() == originalMaterial,
                "conflicting texture GUID registration preserves terrain"))
                return false;
            flatbuffers::FlatBufferBuilder missing;
            const std::array<float, 4> samples{};
            const auto noise = Serialization::CreateTerrainNoiseSettings(missing);
            const auto root = Serialization::CreateTerrainComponentPayload(missing, 2, 2,
                missing.CreateVector(samples.data(), samples.size()), 4, 4, 1, 0, 1, 1, true, noise);
            Serialization::FinishTerrainComponentPayloadBuffer(missing, root);
            if (!checks.expect(!terrain->deserializePayload(3, { missing.GetBufferPointer(), missing.GetSize() })
                && terrain->getMaterial() == originalMaterial, "version 3 requires material table")
                || !checks.expect(restored.deserializePayload(2, { missing.GetBufferPointer(), missing.GetSize() })
                    && !restored.getMaterialSettings().enabled && !restored.getMaterial().isValid(),
                    "legacy version 2 retains default standard terrain appearance"))
                return false;
            for (int invalid = 0; invalid < 4; ++invalid)
            {
                flatbuffers::FlatBufferBuilder malformed;
                const Serialization::Vec4 color(1, 1, 1, 1);
                const auto layer = Serialization::CreateTerrainLayerData(malformed, &color, 0,
                    invalid == 2 ? std::numeric_limits<float>::quiet_NaN() : 1);
                const std::vector layers(invalid == 1 ? 0u : 4u, layer);
                const auto surface = Serialization::CreateTerrainMaterialData(malformed, true, invalid == 0 ? 5u : 4u,
                    malformed.CreateVector(layers));
                const auto recipe = Serialization::CreateTerrainNoiseSettings(malformed);
                const auto component = Serialization::CreateTerrainComponentPayload(malformed, 2, 2,
                    malformed.CreateVector(samples.data(), samples.size()), 4, 4, 1, 0, 1, 1, true, recipe, surface);
                Serialization::FinishTerrainComponentPayloadBuffer(malformed, component);
                const auto bytes = std::span(malformed.GetBufferPointer(), malformed.GetSize());
                const auto input = invalid == 3 ? bytes.first(8) : bytes;
                if (!checks.expect(!terrain->deserializePayload(3, input) && terrain->getModel() == originalModel
                    && terrain->getMaterial() == originalMaterial && sameSurface(terrain->getMaterialSettings(), settings),
                    "malformed layer payload preserves applied terrain transactionally"))
                    return false;
            }
            Scene loaded(ObjectGUID::generate(), "Layer reload");
            if (!checks.expect(Serialization::SceneSerializer{}.save(directory / L"layers.scene", scene)
                && Serialization::SceneSerializer{}.load(directory / L"layers.scene", loaded), "layer scene persistence"))
                return false;
            const auto* loadedObject = loaded.find(object->getGUID());
            const auto* loadedTerrain = loadedObject ? loadedObject->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(loadedTerrain && sameSurface(loadedTerrain->getMaterialSettings(), settings), "scene retains texture GUIDs and all layer controls"))
                return false;
            TerrainNoiseSettings recipe;
            recipe.width = 9;
            recipe.height = 17;
            if (!checks.expect(restored.setMaterialSettings(settings) && restored.generateNoise(recipe, { 8, 8, 4, 0, 1, 1 })
                && sameSurface(restored.getMaterialSettings(), settings), "noise regeneration retains applied material and recomputes layer weights"))
                return false;
            Scene clone(ObjectGUID::generate(), "Layer clone");
            const SceneClonePair pair{ scene, clone };
            if (!checks.expect(cloneScenes(std::span(&pair, 1)), "layer scene Play clone"))
                return false;
            const auto* cloneObject = clone.find(object->getGUID());
            const auto* cloneTerrain = cloneObject ? cloneObject->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(cloneTerrain && sameSurface(cloneTerrain->getMaterialSettings(), settings)
                && cloneTerrain->getMaterial() != originalMaterial, "clone owns independent material with shared texture references"))
                return false;
            Prefab prefab, loadedPrefab;
            Scene instances(ObjectGUID::generate(), "Layer instances");
            if (!checks.expect(prefab.capture(*object) && Serialization::PrefabSerializer{}.save(directory / L"layers.prefab", prefab)
                && Serialization::PrefabSerializer{}.load(directory / L"layers.prefab", loadedPrefab), "layer prefab save and load"))
                return false;
            const auto* instance = loadedPrefab.instantiate(instances);
            const auto* instanceTerrain = instance ? instance->getComponent<TerrainComponent>() : nullptr;
            if (!checks.expect(instanceTerrain && sameSurface(instanceTerrain->getMaterialSettings(), settings), "prefab retains full layered surface"))
                return false;
            MaterialAsset savedMaterial = *material;
            MaterialAsset loadedMaterial;
            if (!checks.expect(Serialization::MaterialSerializer{}.save(directory / L"layers.matl", savedMaterial)
                && Serialization::MaterialSerializer{}.load(directory / L"layers.matl", loadedMaterial)
                && loadedMaterial.terrain && sameSurface(*loadedMaterial.terrain, settings), "standalone material rebases and resolves layer texture hints"))
                return false;
            auto embedded = *ModelManager::instance().get(originalModel);
            embedded.embeddedMaterials = { savedMaterial };
            embedded.materialSlots = { { "Terrain", savedMaterial.guid, {} } };
            ModelResource loadedModel;
            if (!checks.expect(Serialization::ModelSerializer{}.save(directory / L"layers.model", embedded)
                && Serialization::ModelSerializer{}.load(directory / L"layers.model", loadedModel)
                && loadedModel.embeddedMaterials.size() == 1 && loadedModel.embeddedMaterials[0].terrain
                && sameSurface(*loadedModel.embeddedMaterials[0].terrain, settings), "embedded model material retains terrain data and owner-relative references"))
                return false;
            auto& queue = ModelRenderSubmissionQueue::instance();
            queue.clear();
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            queue.consume(submissions);
            if (!checks.expect(submissions.size() == 1 && submissions[0].materialSnapshot == material,
                "queued terrain captures immutable material"))
                return false;
            settings.enabled = false;
            if (!checks.expect(terrain->setMaterialSettings(settings)
                && MaterialManager::instance().get(originalMaterial) == nullptr
                && submissions[0].materialSnapshot->terrain->enabled
                && !terrain->getMaterial().isValid()
                && sameSurface(cloneTerrain->getMaterialSettings(), *material->terrain),
                "disable retires material without changing queued snapshots or clones"))
                return false;
            savedMaterial.renderState.surfaceType = MaterialSurfaceType::Transparent;
            return checks.expect(!MaterialManager::instance().create(savedMaterial).isValid()
                && !MaterialManager::instance().update(cloneTerrain->getMaterial(), savedMaterial)
                && !Serialization::MaterialSerializer{}.save(directory / L"invalid-layer.matl", savedMaterial),
                "layered material create update and save reject transparent state");
        }

        float decodeSrgb(const float value)
        {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        bool expectLayerPixel(Checks& checks, const DX12FrameCapture& capture, const std::size_t x,
            const std::size_t y, const Vector3& expected, const char* label)
        {
            const auto* image = capture.display.GetImage(0, 0, 0);
            if (!checks.expect(image && image->format == DXGI_FORMAT_R8G8B8A8_UNORM && x < image->width && y < image->height,
                "layer capture format and bounds"))
                return false;
            const auto* pixel = image->pixels + y * image->rowPitch + x * 4;
            const Vector3 actual(decodeSrgb(static_cast<float>(pixel[0]) / 255),
                decodeSrgb(static_cast<float>(pixel[1]) / 255), decodeSrgb(static_cast<float>(pixel[2]) / 255));
            const bool matches = Vector3::Distance(actual, expected) < 0.018f;
            if (!matches)
                LOG_ERROR("[TerrainValidation] Pixel ({},{}): actual=({},{},{}) expected=({},{},{}).",
                    x, y, actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
            return checks.expect(matches, label);
        }

        bool validateLayerRendering(Checks& checks, DX12Renderer& renderer, const std::filesystem::path& directory,
            const TerrainLayer& textured)
        {
            Scene scene(ObjectGUID::generate(), "Layer rendering");
            auto* object = scene.createGameObject("Layered terrain");
            auto* terrain = object ? object->addComponent<TerrainComponent>() : nullptr;
            Heightmap heights;
            const TerrainMeshSettings mesh{ 4, 4, 0, 0, 1, 1 };
            if (!checks.expect(terrain && heights.createFlat(65, 65, 0.5f) && terrain->setTerrain(heights, mesh),
                "layer GPU terrain fixture"))
                return false;
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            const auto captureView = [&](const DeferredDebugView view, DX12FrameCapture& capture)
            {
                if (!renderer.setDeferredDebugView(view))
                    return false;
                for (int frame = 0; frame < 2; ++frame)
                    if (!checks.expect(renderFrame(renderer, &scene), "render layered PBR surface"))
                        return false;
                return checks.expect(renderer.captureFrame(capture) && renderer.validateDebugMessages(), "capture layered surface without D3D12 diagnostics");
            };
            TerrainMaterialSettings settings;
            settings.enabled = true;
            DX12FrameCapture capture;
            for (std::size_t selected = 0; selected < TERRAIN_LAYER_COUNT; ++selected)
            {
                settings.layerCount = static_cast<std::uint32_t>(selected + 1);
                for (auto& layer : settings.layers)
                {
                    layer = TerrainLayer{};
                    layer.strength = 0;
                }
                auto& layer = settings.layers[selected];
                layer = textured;
                layer.strength = 1;
                layer.color = Vector4(0.5f, 1, 0.75f, 1);
                layer.metallic = 0.5f;
                layer.roughness = 0.6f;
                layer.normalScale = 0.5f;
                if (!checks.expect(terrain->setMaterialSettings(settings), "apply each of four textured GPU slots")
                    || !captureView(DeferredDebugView::Albedo, capture)
                    || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.10793f, 0.21586f, 0.16190f),
                        "BaseColor is sRGB decoded and multiplied by linear layer color")
                    || !captureView(DeferredDebugView::Metallic, capture)
                    || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.4f), "Metallic uses linear blue texture channel and layer factor")
                    || !captureView(DeferredDebugView::Roughness, capture)
                    || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.30118f), "Roughness uses linear green texture channel and layer factor")
                    || !captureView(DeferredDebugView::Normal, capture)
                    || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.67482f, 0.96844f, 0.49771f),
                        "Normal uses linear data, layer scale and terrain tangent frame"))
                    return false;
                layer.uvRepeat = 2;
                if (!checks.expect(terrain->setMaterialSettings(settings), "apply per-layer UV repeat")
                    || !captureView(DeferredDebugView::Albedo, capture)
                    || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.00722f, 0.05127f, 0.39534f),
                        "each layer UV repeat changes the sampled texture region"))
                    return false;
            }
            settings = {};
            settings.enabled = true;
            const std::array colors = { Vector4(0.4f, 0, 0, 1), Vector4(0, 0.5f, 0, 1),
                Vector4(0, 0, 0.6f, 1), Vector4(0.7f, 0.7f, 0.7f, 1) };
            for (std::size_t index = 0; index < TERRAIN_LAYER_COUNT; ++index)
            {
                settings.layers[index] = TerrainLayer{};
                settings.layers[index].color = colors[index];
                settings.layers[index].minimumHeight = static_cast<float>(index) * 0.25f;
                settings.layers[index].heightBlend = 0.125f;
            }
            std::vector<float> ramp(65 * 2);
            for (std::size_t index = 0; index < ramp.size(); ++index)
                ramp[index] = static_cast<float>(index % 65) / 64;
            if (!checks.expect(heights.assign(65, 2, ramp) && terrain->setTerrain(heights, mesh)
                && terrain->setMaterialSettings(settings), "automatic four-height-band GPU fixture")
                || !captureView(DeferredDebugView::Albedo, capture))
                return false;
            for (std::size_t index = 0; index < TERRAIN_LAYER_COUNT; ++index)
                if (!expectLayerPixel(checks, capture, 80 + index * 32, 128,
                    Vector3(colors[index].x, colors[index].y, colors[index].z), "four height bands select expected colors"))
                    return false;
            const float vertexPosition = (88.5f - 64) * 0.5f;
            const float leftVertex = std::floor(vertexPosition);
            const float fraction = vertexPosition - leftVertex;
            const auto leftWeights = terrainLayerWeights(settings, leftVertex / 64, 0);
            const auto rightWeights = terrainLayerWeights(settings, (leftVertex + 1) / 64, 0);
            const float grassWeight = std::lerp(leftWeights.y, rightWeights.y, fraction);
            if (!expectLayerPixel(checks, capture, 88, 128, Vector3(0.4f * (1 - grassWeight), 0.5f * grassWeight, 0),
                "interpolated vertex weights produce smooth per-pixel layer transitions")
                || !checks.expect(capture.save(directory, L"terrain-layers"), "save automatic layer capture"))
                return false;

            settings.layerCount = 2;
            settings.layers[1].minimumHeight = 0;
            settings.layers[1].minimumSlope = 40;
            settings.layers[1].slopeBlend = 0;
            if (!checks.expect(terrain->setTerrain(heights, { 4, 4, 4, 0, 1, 1 })
                && terrain->setMaterialSettings(settings), "steep GPU terrain fixture")
                || !captureView(DeferredDebugView::Albedo, capture)
                || !expectLayerPixel(checks, capture, 128, 128, Vector3(0, 0.5f, 0), "slope band selects steep surface")
                || !checks.expect(terrain->setTerrain(heights, mesh), "flatten geometry without changing normalized heights")
                || !captureView(DeferredDebugView::Albedo, capture)
                || !expectLayerPixel(checks, capture, 128, 128, Vector3(0.4f, 0, 0), "slope mask updates after mesh settings change"))
                return false;
            settings.layerCount = 1;
            settings.layers[0] = textured;
            settings.layers[0].color = Vector4::One;
            settings.layers[0].normalScale = 0;
            if (!checks.expect(terrain->setMaterialSettings(settings), "normal scale zero fixture")
                || !captureView(DeferredDebugView::Normal, capture)
                || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.5f, 1, 0.5f), "zero normal scale restores geometric normal"))
                return false;
            settings.layers[0].normalScale = 1;
            if (!checks.expect(terrain->setMaterialSettings(settings), "normal transform fixture"))
                return false;
            object->getTransform()->setRotation(Quaternion::CreateFromYawPitchRoll(DirectX::XM_PIDIV2, 0, 0));
            if (!captureView(DeferredDebugView::Normal, capture)
                || !expectLayerPixel(checks, capture, 128, 128, Vector3(0.49805f, 0.90072f, 0.20093f),
                    "normal layer follows object rotation without changing local blend rules"))
                return false;
            object->getTransform()->setRotation(Quaternion::Identity);
            settings.layerCount = 2;
            settings.layers[0] = TerrainLayer{};
            settings.layers[0].color = Vector4(0.4f, 0, 0, 1);
            settings.layers[0].metallic = 0.2f;
            settings.layers[0].roughness = 0.8f;
            settings.layers[1] = textured;
            settings.layers[1].strength = 0.5f;
            settings.layers[1].metallic = 0.6f;
            settings.layers[1].roughness = 0.4f;
            if (!checks.expect(terrain->setMaterialSettings(settings), "two-layer texture and PBR blend fixture")
                || !captureView(DeferredDebugView::Albedo, capture)
                || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.30793f, 0.10793f, 0.10793f), "blend textures and tint in linear space")
                || !captureView(DeferredDebugView::Metallic, capture)
                || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.34f), "metallic blends with normalized layer weights")
                || !captureView(DeferredDebugView::Roughness, capture)
                || !expectLayerPixel(checks, capture, 104, 128, Vector3(0.50039f), "roughness blends with normalized layer weights"))
                return false;
            Vector3 baseNormal(1.0f / 255, 1.0f / 255, 1);
            Vector3 overlayNormal(0.6f, 1.0f / 255, 205.0f / 255);
            baseNormal.Normalize();
            overlayNormal.Normalize();
            Vector3 mixedNormal = baseNormal + overlayNormal;
            mixedNormal.Normalize();
            if (!captureView(DeferredDebugView::Normal, capture)
                || !expectLayerPixel(checks, capture, 104, 128,
                    Vector3(mixedNormal.x, mixedNormal.z, -mixedNormal.y) * 0.5f + Vector3(0.5f),
                    "normal vectors blend and renormalize in a common tangent frame"))
                return false;
            settings.enabled = false;
            if (!checks.expect(terrain->setMaterialSettings(settings), "disable layer GPU fixture")
                || !captureView(DeferredDebugView::Albedo, capture)
                || !expectLayerPixel(checks, capture, 128, 128, Vector3::One, "disabled layered terrain retains default material appearance"))
                return false;

            settings = {};
            settings.enabled = true;
            settings.layerCount = 1;
            settings.layers[0].color = Vector4(0.6f, 0.03f, 0.01f, 1);
            renderer.setDeferredDebugView(DeferredDebugView::FinalLighting);
            for (const bool deferred : { true, false })
            {
                renderer.setDeferredRenderingEnabled(deferred);
                renderer.setOcclusionCullingEnabled(!deferred);
                if (!checks.expect(terrain->setMaterialSettings(settings), "layered Forward/Deferred fixture"))
                    return false;
                for (int frame = 0; frame < 4; ++frame)
                    if (!checks.expect(renderFrame(renderer, &scene), "Forward/Deferred layered surface with occlusion"))
                        return false;
                if (!checks.expect(renderer.captureFrame(capture) && renderer.validateDebugMessages(), "Forward/Deferred layered capture"))
                    return false;
                const auto* image = capture.display.GetImage(0, 0, 0);
                const auto* pixel = image ? image->pixels + 128 * image->rowPitch + 128 * 4 : nullptr;
                if (!checks.expect(pixel && pixel[0] > pixel[1] + 15 && pixel[0] > pixel[2] + 15,
                    "Forward/Deferred final lighting uses terrain layer color"))
                    return false;
            }
            renderer.setDeferredRenderingEnabled(true);
            renderer.setOcclusionCullingEnabled(false);
            renderer.setDeferredDebugView(DeferredDebugView::Albedo);
            settings.layers[0].color = Vector4(0.1f, 0.3f, 0.7f, 1);
            if (!checks.expect(terrain->setMaterialSettings(settings), "shared-handle material snapshots fixture"))
                return false;
            scene.submitRenderData();
            std::vector<ModelRenderSubmission> submissions;
            ModelRenderSubmissionQueue::instance().consume(submissions);
            if (!checks.expect(submissions.size() == 1, "capture procedural submission for snapshot isolation test"))
                return false;
            auto old = submissions[0];
            old.worldMatrix = Matrix::CreateScale(0.4f) * Matrix::CreateTranslation(-1, 0, 0);
            old.modelSnapshot->boundingBox.Transform(old.worldBounds, old.worldMatrix);
            old.objectID = allocateModelRenderObjectID();
            auto updated = old;
            auto edited = *old.materialSnapshot;
            edited.terrain->layers[0].color = Vector4(0.7f, 0.1f, 0.2f, 1);
            if (!checks.expect(MaterialManager::instance().update(terrain->getMaterial(), std::move(edited)), "update live material after taking immutable snapshot"))
                return false;
            updated.materialSnapshot = MaterialManager::instance().get(terrain->getMaterial());
            updated.worldMatrix = Matrix::CreateScale(0.4f) * Matrix::CreateTranslation(1, 0, 0);
            updated.modelSnapshot->boundingBox.Transform(updated.worldBounds, updated.worldMatrix);
            updated.objectID = allocateModelRenderObjectID();
            for (int frame = 0; frame < 4; ++frame)
            {
                ModelRenderSubmissionQueue::instance().submit(old);
                ModelRenderSubmissionQueue::instance().submit(updated);
                if (!checks.expect(renderFrame(renderer, nullptr) && renderer.captureFrame(capture) && renderer.validateDebugMessages(),
                    "multiple snapshots of one handle remain fenced and cannot instance together")
                    || !expectLayerPixel(checks, capture, 96, 128, Vector3(0.1f, 0.3f, 0.7f), "old queued snapshot is not replaced by current manager state")
                    || !expectLayerPixel(checks, capture, 160, 128, Vector3(0.7f, 0.1f, 0.2f), "new queued snapshot keeps independent GPU constants"))
                    return false;
            }
            settings.layers[0].color = Vector4(0.1f, 0.3f, 0.7f, 1);
            if (!checks.expect(terrain->setMaterialSettings(settings), "new unuploaded terrain material"))
                return false;
            scene.submitRenderData();
            const auto retiredMaterial = terrain->getMaterial();
            const auto retiredModel = terrain->getModel();
            settings.layers[0].color = Vector4(1, 0, 0, 1);
            if (!checks.expect(terrain->setMaterialSettings(settings), "replace material after queue submission"))
                return false;
            scene.clear();
            if (!checks.expect(MaterialManager::instance().get(retiredMaterial) == nullptr
                && ModelManager::instance().get(retiredModel) == nullptr
                && renderFrame(renderer, nullptr) && renderer.captureFrame(capture) && renderer.validateDebugMessages(),
                "queued unuploaded geometry and material survive rebuild and component destruction")
                || !expectLayerPixel(checks, capture, 128, 128, Vector3(0.1f, 0.3f, 0.7f), "queued immutable material draws original surface"))
                return false;
            renderer.setDeferredDebugView(DeferredDebugView::FinalLighting);
            for (int frame = 0; frame < 6; ++frame)
                if (!checks.expect(renderFrame(renderer, nullptr), "retire layered GPU resources after fences"))
                    return false;
            return checks.expect(renderer.validateDebugMessages(), "layered GPU cleanup without diagnostics");
        }

        bool validateChunkRendering(Checks& checks, DX12Renderer& renderer, const std::filesystem::path& directory,
            const TerrainLayer& textured)
        {
            Scene scene(ObjectGUID::generate(), "Chunk GPU validation");
            auto* object = scene.createGameObject("Chunked surface");
            auto* terrain = object->addComponent<TerrainComponent>();
            TerrainNoiseSettings noise;
            noise.width = 41;
            noise.height = 36;
            noise.seed = 413;
            TerrainMaterialSettings material;
            material.enabled = true;
            material.layers[0] = textured;
            if (!checks.expect(terrain->generateNoise(noise, { 4, 4, 1, 0, 3, 5 })
                && terrain->setMaterialSettings(material), "textured nonflat partial-chunk GPU fixture"))
                return false;
            const auto captureScene = [&](DX12FrameCapture& capture)
            {
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(renderFrame(renderer, &scene), "render resident chunk grid"))
                        return false;
                return checks.expect(renderer.captureFrame(capture) && renderer.validateDebugMessages(),
                    "capture all chunk meshes without D3D12 diagnostics");
            };
            const auto compare = [&](const DX12FrameCapture& reference, const DX12FrameCapture& actual)
            {
                const auto* a = reference.display.GetImage(0, 0, 0);
                const auto* b = actual.display.GetImage(0, 0, 0);
                if (!checks.expect(a && b && a->format == DXGI_FORMAT_R8G8B8A8_UNORM && b->format == a->format
                    && a->width == b->width && a->height == b->height, "chunk capture shape"))
                    return false;
                int maximumDifference = 0;
                std::size_t changed = 0;
                for (std::size_t row = 0; row < a->height; ++row)
                    for (std::size_t column = 0; column < a->width * 4; ++column)
                    {
                        const int delta = std::abs(static_cast<int>(a->pixels[row * a->rowPitch + column])
                            - static_cast<int>(b->pixels[row * b->rowPitch + column]));
                        maximumDifference = std::max(maximumDifference, delta);
                        if (delta > 2)
                            ++changed;
                    }
                if (changed)
                    LOG_ERROR("[TerrainChunks] GPU partition changed {} channels; maximum difference={}.", changed, maximumDifference);
                return checks.expect(changed == 0, "chunked and whole surfaces agree at every pixel within two UNORM steps");
            };
            DX12FrameCapture reference, capture;
            for (const bool deferred : { true, false })
            {
                renderer.setDeferredRenderingEnabled(deferred);
                renderer.setOcclusionCullingEnabled(!deferred);
                const auto views = deferred
                    ? std::vector{ DeferredDebugView::Albedo, DeferredDebugView::Normal, DeferredDebugView::FinalLighting }
                    : std::vector{ DeferredDebugView::FinalLighting };
                for (const auto view : views)
                {
                    if (!renderer.setDeferredDebugView(view)
                        || !checks.expect(terrain->setChunkSettings({ 1024 }), "whole terrain GPU reference")
                        || !captureScene(reference))
                        return false;
                    for (const std::uint32_t edge : { 16u, 17u, 32u })
                        if (!checks.expect(terrain->setChunkSettings({ edge }), "GPU repartition keeps applied surface")
                            || !captureScene(capture) || !compare(reference, capture))
                            return false;
                    if (deferred && view == DeferredDebugView::FinalLighting
                        && !checks.expect(capture.save(directory, L"terrain-chunks"), "save chunked terrain GPU captures"))
                        return false;
                }
            }
            if (!checks.expect(terrain->setChunkSettings({ 17 }), "unuploaded chunk lifetime fixture"))
                return false;
            scene.submitRenderData();
            const auto retired = terrain->getModel();
            scene.clear();
            if (!checks.expect(ModelManager::instance().get(retired) == nullptr
                && renderFrame(renderer, nullptr) && renderer.captureFrame(capture) && renderer.validateDebugMessages(),
                "queued unuploaded chunk model survives destruction")
                || !compare(reference, capture))
                return false;
            for (int frame = 0; frame < 6; ++frame)
                if (!checks.expect(renderFrame(renderer, nullptr), "retire all chunk mesh buffers behind GPU fences"))
                    return false;
            Scene smallScene(ObjectGUID::generate(), "Maximum chunk GPU reference");
            auto* referenceTerrain = smallScene.createGameObject("Reference")->addComponent<TerrainComponent>();
            auto* maximum = scene.createGameObject("4096 chunks")->addComponent<TerrainComponent>();
            Heightmap flat, large;
            const TerrainMeshSettings flatSettings{ 4, 4, 0, 0, 3, 5 };
            if (!checks.expect(flat.createFlat(2, 2, 0.5f) && large.createFlat(1025, 1025, 0.5f)
                && referenceTerrain->setTerrain(flat, flatSettings) && referenceTerrain->setMaterialSettings(material)
                && maximum->setChunkSettings({ 16 }) && maximum->setMaterialSettings(material)
                && maximum->setTerrain(large, flatSettings) && maximum->getChunks().size() == 4096,
                "maximum 4096-mesh GPU fixture with equivalent two-triangle reference"))
                return false;
            for (const bool deferred : { true, false })
            {
                renderer.setDeferredRenderingEnabled(deferred);
                renderer.setOcclusionCullingEnabled(!deferred);
                for (int frame = 0; frame < 3; ++frame)
                    if (!checks.expect(renderFrame(renderer, &smallScene), "render maximum-grid reference"))
                        return false;
                if (!checks.expect(renderer.captureFrame(reference), "capture maximum-grid reference")
                    || !captureScene(capture) || !compare(reference, capture))
                    return false;
            }
            if (!checks.expect(maximum->setLodSettings({ true, 4, 1 }), "enable LOD at maximum 4096 chunks")
                || !captureScene(capture) || !compare(reference, capture))
                return false;
            const auto stats = renderer.getStatistics();
            if (!checks.expect(stats.terrainVisibleChunks == 4096 && stats.terrainTriangles * 2 < stats.terrainFullTriangles,
                "maximum chunk GPU fixture actually selects reduced triangle ranges"))
                return false;
            LOG_INFO("[TerrainLodValidation] Maximum grid: {} visible chunks, {} / {} color triangles.",
                stats.terrainVisibleChunks, stats.terrainTriangles, stats.terrainFullTriangles);
            scene.clear();
            smallScene.clear();
            for (int frame = 0; frame < 6; ++frame)
                if (!checks.expect(renderFrame(renderer, nullptr), "retire maximum-grid GPU resources"))
                    return false;
            if (!checks.expect(renderer.validateDebugMessages(), "maximum-grid GPU teardown has no diagnostics"))
                return false;
            return true;
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
            TerrainLayer textured;
            std::size_t collisionChecks = 0;
            std::size_t lodChecks = 0;
            std::size_t backgroundChecks = 0;
            std::size_t erosionChecks = 0;
            std::size_t gpuChecks = 0;
            if (!validateOptions(checks) || !validateHeightmap(checks, directory, imported) || !validateNoise(checks) || !validateMesh(checks)
                || !validateComponent(checks, directory, imported) || !validateNoisePersistence(checks, directory)
                || !validateInspector(checks)
                || !validateLayerWeights(checks) || !writeLayerTextures(checks, directory, textured)
                || !validateLayerPersistence(checks, directory, textured)
                || !validateChunks(checks, directory)
                || !validateTerrainCollision(directory, collisionChecks)
                || !validateRendering(checks, renderer, directory)
                || !validateLayerRendering(checks, renderer, directory, textured)
                || !validateChunkRendering(checks, renderer, directory, textured)
                || !validateTerrainLod(renderer, directory, lodChecks)
                || !validateTerrainBackground(renderer, directory, backgroundChecks)
                || !validateTerrainErosion(directory, erosionChecks)
                || !validateTerrainGpu(directory, gpuChecks)
                || !checks.expect(renderer.validateDebugMessages(), "all terrain Compute/render work finishes without D3D12 diagnostics"))
                return false;
            checks.count += collisionChecks;
            checks.count += lodChecks;
            checks.count += backgroundChecks;
            checks.count += erosionChecks;
            checks.count += gpuChecks;
            std::ofstream report(directory / L"terrain-results.txt");
            report << "PASS: " << checks.count << " terrain checks\n"
                << "8/16-bit image precision, mesh geometry, scene/prefab/clone, Inspector, lifecycle,\n"
                << "Perlin/FBM reproducibility, parameter bounds, Deferred/Forward noise terrain rendering,\n"
                << "four-layer height/slope blends, texture/PBR channels, UV repeat, normal transforms,\n"
                << "layer Scene/Prefab/Play/MATL/model roundtrips, queued resource lifetime and D3D12 diagnostics.\n";
            report << "Triangle-exact local/world surfaces, PhysX queries, terrain collision regeneration,\n"
                << "Rigidbody contacts/events, Character Controller grounding/walking, filters and persistence.\n";
            report << "Chunk coverage, exact shared vertex attributes, partial edges, 4096-chunk maximum,\n"
                << "payload v4 migration, compound contacts, cross-seam walking and pixel-equivalent GPU partitions.\n";
            report << "Opt-in distance LOD, exact full-resolution boundary edges at all levels, manifold/area checks,\n"
                << "payload v5/legacy migration, immutable queued LOD snapshots, unchanged cooked physics,\n"
                << "mixed-LOD pixel comparisons, per-camera chunk culling/LOD and independent full-resolution shadows.\n";
            report << "Background build parity, latest-wins/cancel/lifetime, applied-only persistence, busy-physics rollback,\n"
                << "independent CPU thread/owner-thread commit, maximum-grid concurrent frames and atomic GPU snapshots.\n";
            report << "Hydraulic droplet erosion: deterministic paths, sediment conservation/export, normalized bounds,\n"
                << "v6 recipe migration, applied-only persistence, async parity/cancellation, rebuilt PhysX and CCT grounding.\n";
            report << "GPU Compute Perlin/FBM: actual dispatch/readback, 2e-6 CPU error bound at all tested samples,\n"
                << "extreme coordinates, deterministic repeats, applied persistence, cancellation/lifetime and exact PhysX parity.\n";
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
