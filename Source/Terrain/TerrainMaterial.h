#pragma once

#include <array>
#include <filesystem>
#include "Assets\Material\MaterialTypes.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    class Heightmap;
    struct ModelResource;

    inline constexpr std::size_t TERRAIN_LAYER_COUNT = 4;
    inline constexpr std::size_t TERRAIN_LAYER_TEXTURE_COUNT = TERRAIN_LAYER_COUNT * 3;

    /** @brief One opaque PBR layer. Texture channels use Standard material conventions. */
    struct TerrainLayer
    {
        Vector4 color = Vector4::One;
        float metallic = 0.0f;
        float roughness = 1.0f;
        float normalScale = 1.0f;
        float uvRepeat = 1.0f;
        float minimumHeight = 0.0f;
        float maximumHeight = 1.0f;
        float heightBlend = 0.1f;
        float minimumSlope = 0.0f;
        float maximumSlope = 90.0f;
        float slopeBlend = 5.0f;
        float strength = 1.0f;
        AssetGUID baseColorTexture;
        AssetGUID normalTexture;
        AssetGUID metallicRoughnessTexture;
        std::filesystem::path baseColorPath;
        std::filesystem::path normalPath;
        std::filesystem::path metallicRoughnessPath;
    };

    /**
     * @brief Four-layer authoring data. Disabled terrain retains the standard default material.
     * @details Layer zero is the fallback; higher indices overlay earlier layers in order.
     * @thread_safety Concurrent reads are safe; mutations require external synchronization.
     */
    struct TerrainMaterialSettings
    {
        bool enabled = false;
        std::uint32_t layerCount = 4;
        std::array<TerrainLayer, TERRAIN_LAYER_COUNT> layers;

        /** @brief Creates soil, grass, rock and snow defaults without external textures. */
        TerrainMaterialSettings();
    };

    /**
     * @brief Validates every layer, including inactive ones, and logs invalid numeric/reference data.
     * @param settings Colors [0,1], metallic/roughness/strength [0,1], normal scale [0,2],
     * UV repeat [0.001,10000], normalized height [0,1], slope [0,90] degrees;
     * ordered inclusive ranges, height blend [0,1] and slope blend [0,90].
     * @return True only for finite parameters, 1..4 layers and GUID/path pairs without path-only references.
     */
    bool validateTerrainMaterial(const TerrainMaterialSettings& settings);

    /**
     * @brief Computes ordered automatic layer weights in local terrain space.
     * @param settings Validated surface settings.
     * @param height Normalized height sample in [0,1].
     * @param slope Local-space slope in degrees [0,90].
     * @return Nonnegative normalized RGBA weights. Layer zero fills uncovered regions.
     */
    Vector4 terrainLayerWeights(const TerrainMaterialSettings& settings, float height, float slope) noexcept;

    /**
     * @brief Writes automatic blend weights to generated vertex colors without changing geometry.
     * @param heights Source samples, matching the single terrain mesh vertex count.
     * @param settings Validated layer settings; disabled surfaces use white vertex colors.
     * @param model Generated single-mesh terrain model.
     * @return True on success; invalid input is logged before any vertex is changed.
     */
    bool applyTerrainLayerWeights(const Heightmap& heights, const TerrainMaterialSettings& settings, ModelResource& model);

    /**
     * @brief Resolves texture hints and registers GUID/path pairs without loading GPU textures.
     * @param settings Validated settings, updated to absolute paths; GUID-only references use the registry.
     * @param owner Owning asset file, or empty for working-directory-relative input.
     * @return True on success. Failure is logged; callers should supply a temporary copy.
     */
    bool resolveTerrainMaterialTextures(TerrainMaterialSettings& settings, const std::filesystem::path& owner = {});
}
