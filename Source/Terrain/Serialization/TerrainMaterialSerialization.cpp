#include "Pch.h"
#include "Terrain\Serialization\TerrainMaterialSerialization.h"
#include "Core\Serialization\AssetReferencePaths.h"
#include "Core\Logging\Logger.h"
#include "Core\Containers\String.h"
#include "Core\Math\MathTypes.h"

namespace Engine::Serialization
{
    bool writeTerrainMaterial(flatbuffers::FlatBufferBuilder& builder, const Engine::TerrainMaterialSettings& settings,
        flatbuffers::Offset<TerrainMaterialData>& result, const std::filesystem::path& owner)
    {
        if (!validateTerrainMaterial(settings))
            return false;
        std::vector<flatbuffers::Offset<TerrainLayerData>> layers;
        for (const auto& layer : settings.layers)
        {
            std::array paths = { layer.baseColorPath, layer.normalPath, layer.metallicRoughnessPath };
            if (!owner.empty())
                for (auto& path : paths)
                    if (!relativizeAssetReference(path, {}, owner, path))
                        return false;
            const Vec4 color(layer.color.x, layer.color.y, layer.color.z, layer.color.w);
            const AssetGuid base(layer.baseColorTexture.high, layer.baseColorTexture.low);
            const AssetGuid normal(layer.normalTexture.high, layer.normalTexture.low);
            const AssetGuid mr(layer.metallicRoughnessTexture.high, layer.metallicRoughnessTexture.low);
            layers.push_back(CreateTerrainLayerData(builder, &color, layer.metallic, layer.roughness, layer.normalScale,
                layer.uvRepeat, layer.minimumHeight, layer.maximumHeight, layer.heightBlend, layer.minimumSlope,
                layer.maximumSlope, layer.slopeBlend, layer.strength, &base, &normal, &mr,
                builder.CreateString(paths[0].generic_string()), builder.CreateString(paths[1].generic_string()),
                builder.CreateString(paths[2].generic_string())));
        }
        result = CreateTerrainMaterialData(builder, settings.enabled, settings.layerCount, builder.CreateVector(layers));
        return true;
    }

    bool readTerrainMaterial(const TerrainMaterialData* source, Engine::TerrainMaterialSettings& settings,
        const std::filesystem::path& owner)
    {
        if (source == nullptr || source->layers() == nullptr || source->layers()->size() != TERRAIN_LAYER_COUNT)
        {
            LOG_ERROR("[TerrainMaterial] Serialized material must contain exactly four layer slots.");
            return false;
        }
        Engine::TerrainMaterialSettings loaded;
        loaded.enabled = source->enabled();
        loaded.layerCount = source->layer_count();
        const auto guid = [](const AssetGuid* value)
        {
            return value != nullptr ? AssetGUID{ value->high(), value->low() } : AssetGUID{};
        };
        const auto path = [](const flatbuffers::String* value)
        {
            return value != nullptr ? std::filesystem::path(value->str()) : std::filesystem::path{};
        };
        for (std::size_t index = 0; index < TERRAIN_LAYER_COUNT; ++index)
        {
            const auto* saved = source->layers()->Get(static_cast<flatbuffers::uoffset_t>(index));
            if (saved == nullptr || saved->color() == nullptr)
            {
                LOG_ERROR("[TerrainMaterial] Missing serialized layer color.");
                return false;
            }
            auto& layer = loaded.layers[index];
            layer.color = Vector4(saved->color()->x(), saved->color()->y(), saved->color()->z(), saved->color()->w());
            layer.metallic = saved->metallic();
            layer.roughness = saved->roughness();
            layer.normalScale = saved->normal_scale();
            layer.uvRepeat = saved->uv_repeat();
            layer.minimumHeight = saved->minimum_height();
            layer.maximumHeight = saved->maximum_height();
            layer.heightBlend = saved->height_blend();
            layer.minimumSlope = saved->minimum_slope();
            layer.maximumSlope = saved->maximum_slope();
            layer.slopeBlend = saved->slope_blend();
            layer.strength = saved->strength();
            layer.baseColorTexture = guid(saved->base_color_texture());
            layer.normalTexture = guid(saved->normal_texture());
            layer.metallicRoughnessTexture = guid(saved->metallic_roughness_texture());
            layer.baseColorPath = path(saved->base_color_path());
            layer.normalPath = path(saved->normal_path());
            layer.metallicRoughnessPath = path(saved->metallic_roughness_path());
        }
        if (!resolveTerrainMaterialTextures(loaded, owner))
            return false;
        settings = std::move(loaded);
        return true;
    }
}
