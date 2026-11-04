#include "Pch.h"
#include "Terrain\Material\TerrainMaterial.h"
#include "Terrain\Data\Heightmap.h"
#include "Assets\Model\Resource\ModelResource.h"
#include "Core\Serialization\AssetReferencePaths.h"
#include "Graphics\Texture\TextureManager.h"
#include <cmath>
#include <numbers>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathTypes.h"
#include "Core\Math\MathFunction.h"

namespace Engine
{
    TerrainMaterialSettings::TerrainMaterialSettings()
    {
        layers[0].color = Vector4(0.25f, 0.12f, 0.045f, 1);
        layers[1].color = Vector4(0.08f, 0.28f, 0.035f, 1);
        layers[1].minimumHeight = 0.15f;
        layers[1].maximumHeight = 0.7f;
        layers[1].maximumSlope = 40;
        layers[2].color = Vector4(0.3f, 0.32f, 0.35f, 1);
        layers[2].minimumSlope = 35;
        layers[2].roughness = 0.8f;
        layers[3].color = Vector4(0.85f, 0.9f, 0.95f, 1);
        layers[3].minimumHeight = 0.8f;
        layers[3].maximumSlope = 50;
    }

    bool validateTerrainMaterial(const TerrainMaterialSettings& settings)
    {
        const auto range = [](const float value, const float minimum, const float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        };
        bool valid = settings.layerCount >= 1 && settings.layerCount <= TERRAIN_LAYER_COUNT;
        for (const auto& layer : settings.layers)
        {
            valid = valid && range(layer.color.x, 0, 1) && range(layer.color.y, 0, 1)
                && range(layer.color.z, 0, 1) && layer.color.w == 1
                && range(layer.metallic, 0, 1) && range(layer.roughness, 0, 1)
                && range(layer.normalScale, 0, 2) && range(layer.uvRepeat, 0.001f, 10000)
                && range(layer.minimumHeight, 0, 1) && range(layer.maximumHeight, layer.minimumHeight, 1)
                && range(layer.heightBlend, 0, 1)
                && range(layer.minimumSlope, 0, 90) && range(layer.maximumSlope, layer.minimumSlope, 90)
                && range(layer.slopeBlend, 0, 90) && range(layer.strength, 0, 1)
                && (layer.baseColorPath.empty() || layer.baseColorTexture.isValid())
                && (layer.normalPath.empty() || layer.normalTexture.isValid())
                && (layer.metallicRoughnessPath.empty() || layer.metallicRoughnessTexture.isValid());
        }
        if (!valid)
            LOG_ERROR("[TerrainMaterial] Invalid layer count, PBR values, blend ranges or texture GUID/path pair.");
        return valid;
    }

    Vector4 terrainLayerWeights(const TerrainMaterialSettings& settings, const float height, const float slope) noexcept
    {
        const auto band = [](const float value, const float minimum, const float maximum, const float blend)
        {
            if (value >= minimum && value <= maximum)
                return 1.0f;
            if (blend == 0)
                return 0.0f;
            const float distance = value < minimum ? minimum - value : value - maximum;
            const float t = std::clamp(1.0f - distance / blend, 0.0f, 1.0f);
            return t * t * (3 - 2 * t);
        };
        std::array<float, TERRAIN_LAYER_COUNT> weights{ 1, 0, 0, 0 };
        for (std::size_t index = 1; index < settings.layerCount; ++index)
        {
            const auto& layer = settings.layers[index];
            const float coverage = layer.strength
                * band(height, layer.minimumHeight, layer.maximumHeight, layer.heightBlend)
                * band(slope, layer.minimumSlope, layer.maximumSlope, layer.slopeBlend);
            for (std::size_t previous = 0; previous < index; ++previous)
                weights[previous] *= 1 - coverage;
            weights[index] = coverage;
        }
        return Vector4(weights[0], weights[1], weights[2], weights[3]);
    }

    bool applyTerrainLayerWeights(const Heightmap& heights, const TerrainMaterialSettings& settings, ModelResource& model)
    {
        if (!validateTerrainMaterial(settings))
            return false;
        if (model.meshes.size() != 1 || model.meshes[0].vertices.size() != heights.samples().size())
        {
            LOG_ERROR("[TerrainMaterial] Layer weights require a generated terrain mesh matching the heightmap.");
            return false;
        }
        auto& vertices = model.meshes[0].vertices;
        for (std::size_t index = 0; index < vertices.size(); ++index)
        {
            const float slope = std::acos(std::clamp(vertices[index].normal.y, 0.0f, 1.0f))
                * 180.0f / std::numbers::pi_v<float>;
            vertices[index].color = settings.enabled
                ? terrainLayerWeights(settings, heights.samples()[index], slope) : Vector4::One;
        }
        return true;
    }

    bool resolveTerrainMaterialTextures(TerrainMaterialSettings& settings, const std::filesystem::path& owner)
    {
        if (!validateTerrainMaterial(settings))
            return false;
        auto& textures = TextureManager::instance();
        for (auto& layer : settings.layers)
        {
            const std::array references = {
                std::pair{ &layer.baseColorTexture, &layer.baseColorPath },
                std::pair{ &layer.normalTexture, &layer.normalPath },
                std::pair{ &layer.metallicRoughnessTexture, &layer.metallicRoughnessPath },
            };
            for (const auto& [guid, path] : references)
            {
                if (!guid->isValid())
                    continue;
                if (path->empty())
                    *path = textures.getAssetPath(*guid);
                if (!Serialization::resolveAssetReference(*path, owner, *path)
                    || (!path->empty() && !textures.registerAssetPath(*guid, *path)))
                {
                    LOG_ERROR("[TerrainMaterial] Cannot resolve layer texture reference.");
                    return false;
                }
            }
        }
        return true;
    }
}
