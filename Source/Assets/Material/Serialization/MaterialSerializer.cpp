#include "Pch.h"
#include <flatbuffers/flatbuffers.h>
#include "Generated\FlatBuffers\Material_generated.h"
#include "Assets\Material\Serialization\MaterialSerializer.h"
#include "Core\Logging\Logging.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"

namespace Engine::Serialization
{
    namespace
    {
        AssetGuid toFlat(const AssetGUID& value) { return AssetGuid(value.high, value.low); }
        Vec3 toFlat(const Vector3& value) { return Vec3(value.x, value.y, value.z); }
        Vec4 toFlat(const Vector4& value) { return Vec4(value.x, value.y, value.z, value.w); }

        AssetGUID toEngine(const AssetGuid* value)
        {
            return value ? AssetGUID{ value->high(), value->low() } : AssetGUID{};
        }

        bool readRenderState(const Engine::Serialization::MaterialRenderState* source, Engine::MaterialRenderState& state)
        {
            if (source == nullptr ||
                source->surface_type() > MaterialSurfaceType_Transparent ||
                source->cull_mode() > MaterialCullMode_Back ||
                source->depth_test() > MaterialDepthTest_Always ||
                source->blend_mode() > MaterialBlendMode_Additive)
                return false;

            state.surfaceType = static_cast<Engine::MaterialSurfaceType>(source->surface_type());
            state.cullMode = static_cast<Engine::MaterialCullMode>(source->cull_mode());
            state.depthTest = static_cast<Engine::MaterialDepthTest>(source->depth_test());
            state.blendMode = static_cast<Engine::MaterialBlendMode>(source->blend_mode());
            state.depthWrite = source->depth_write();
            state.renderQueueOffset = source->render_queue_offset();
            return true;
        }
    }

    bool MaterialSerializer::save(const std::filesystem::path& path, const MaterialAsset& material) const
    {
        if (!material.guid.isValid()) {
            LOG_ERROR("Cannot save Material Asset with an invalid GUID: {}", path.string());
            return false;
        }

        flatbuffers::FlatBufferBuilder builder(512);
        const AssetGuid guid = toFlat(material.guid);
        const AssetGuid shaderGuid = toFlat(material.shaderGuid);
        const AssetGuid baseColorTexture = toFlat(material.textures.baseColor);
        const AssetGuid normalTexture = toFlat(material.textures.normal);
        const AssetGuid metallicRoughnessTexture = toFlat(material.textures.metallicRoughness);
        const AssetGuid ambientOcclusionTexture = toFlat(material.textures.ambientOcclusion);
        const AssetGuid emissiveTexture = toFlat(material.textures.emissive);
        const Vec4 baseColor = toFlat(material.baseColor);
        const Vec3 emissiveColor = toFlat(material.emissiveColor);

        const auto renderState = CreateMaterialRenderState(builder,
            static_cast<Engine::Serialization::MaterialSurfaceType>(material.renderState.surfaceType),
            static_cast<Engine::Serialization::MaterialCullMode>(material.renderState.cullMode),
            static_cast<Engine::Serialization::MaterialDepthTest>(material.renderState.depthTest),
            static_cast<Engine::Serialization::MaterialBlendMode>(material.renderState.blendMode),
            material.renderState.depthWrite, material.renderState.renderQueueOffset);
        const auto textures = CreateMaterialTextureReferences(builder, &baseColorTexture, &normalTexture,
            &metallicRoughnessTexture, &ambientOcclusionTexture, &emissiveTexture);

        const auto data = CreateMaterialAssetData(builder, &guid, builder.CreateString(material.name), &shaderGuid,
            renderState, &baseColor, material.metallic, material.roughness, &emissiveColor,
            material.emissiveIntensity, material.normalScale, material.occlusionStrength, material.alphaCutoff,
            textures, 0, material.shaderKeywords & VALID_MATERIAL_KEYWORDS);
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_MATERIAL_VERSION, 0);
        FinishMaterialFileBuffer(builder, CreateMaterialFile(builder, header, data));
        return FlatBufferWriter{}.saveAtomic(path,
            std::span<const std::uint8_t>(builder.GetBufferPointer(), builder.GetSize()));
    }

    bool MaterialSerializer::load(const std::filesystem::path& path, MaterialAsset& material) const
    {
        FlatBufferReader reader;
        if (!reader.open(path))
            return false;
        if (!reader.hasIdentifier("MATL")) {
            LOG_ERROR("FlatBuffers material has an invalid file identifier: {}", path.string());
            return false;
        }

        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!VerifyMaterialFileBuffer(verifier)) {
            LOG_ERROR("FlatBuffers material verification failed: {}", path.string());
            return false;
        }

        const MaterialFile* file = GetMaterialFile(reader.data());
        const MaterialAssetData* source = file ? file->material() : nullptr;
        if (file == nullptr || file->header() == nullptr || source == nullptr || source->guid() == nullptr ||
            source->name() == nullptr || source->render_state() == nullptr || source->base_color() == nullptr ||
            source->emissive_color() == nullptr || source->textures() == nullptr ||
            file->header()->schema_version() != CURRENT_SCHEMA_VERSION ||
            file->header()->asset_version() < MINIMUM_SUPPORTED_MATERIAL_VERSION ||
            file->header()->asset_version() > CURRENT_MATERIAL_VERSION) {
            LOG_ERROR("Unsupported or invalid material version: {}", path.string());
            return false;
        }

        MaterialAsset loaded;
        loaded.guid = toEngine(source->guid());
        if (!loaded.guid.isValid() || !readRenderState(source->render_state(), loaded.renderState)) {
            LOG_ERROR("Material contains an invalid GUID or render state: {}", path.string());
            return false;
        }

        loaded.name = source->name()->str();
        loaded.shaderGuid = toEngine(source->shader_guid());
        loaded.baseColor = Vector4(source->base_color()->x(), source->base_color()->y(), source->base_color()->z(), source->base_color()->w());
        loaded.metallic = source->metallic();
        loaded.roughness = source->roughness();
        loaded.emissiveColor = Vector3(source->emissive_color()->x(), source->emissive_color()->y(), source->emissive_color()->z());
        loaded.emissiveIntensity = source->emissive_intensity();
        loaded.normalScale = source->normal_scale();
        loaded.occlusionStrength = source->occlusion_strength();
        loaded.alphaCutoff = source->alpha_cutoff();

        const auto* textures = source->textures();
        loaded.textures.baseColor = toEngine(textures->base_color());
        loaded.textures.normal = toEngine(textures->normal());
        loaded.textures.metallicRoughness = toEngine(textures->metallic_roughness());
        loaded.textures.ambientOcclusion = toEngine(textures->ambient_occlusion());
        loaded.textures.emissive = toEngine(textures->emissive());
        if (file->header()->asset_version() >= 2)
        {
            if ((source->keyword_mask() & ~VALID_MATERIAL_KEYWORDS) != 0) {
                LOG_WARNING("Material contains unsupported shader keyword bits: {}", path.string());
            }
            loaded.shaderKeywords = source->keyword_mask() & VALID_MATERIAL_KEYWORDS;
        }
        else if (const auto* keywords = source->shader_keywords())
        {
            for (const flatbuffers::String* keyword : *keywords)
            {
                if (keyword == nullptr)
                    return false;
                if (keyword->string_view() == "USE_NORMAL_MAP") loaded.shaderKeywords |= toMask(MaterialKeyword::UseNormalMap);
                else if (keyword->string_view() == "USE_EMISSIVE_MAP") loaded.shaderKeywords |= toMask(MaterialKeyword::UseEmissiveMap);
                else if (keyword->string_view() == "USE_SKINNING") loaded.shaderKeywords |= toMask(MaterialKeyword::UseSkinning);
                else if (keyword->string_view() == "USE_ALPHA_TEST") loaded.shaderKeywords |= toMask(MaterialKeyword::UseAlphaTest);
                else LOG_WARNING("Ignoring unsupported Material shader keyword '{}': {}", keyword->str(), path.string());
            }
        }

        material = std::move(loaded);
        return true;
    }
} // namespace Engine::Serialization