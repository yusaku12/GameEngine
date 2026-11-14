#include "Pch.h"
#include "Assets\PostProcess\PostProcessOcioAsset.h"
#include "Core\Logging\Logger.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Generated\FlatBuffers\PostProcessOcio_generated.h"
#include <cmath>

namespace Engine
{
    namespace
    {
        bool validAsset(const PostProcessOcioAsset& asset)
        {
            const auto validText = [](const std::string& value, const bool required)
            {
                return (!required || !value.empty()) && value.find('\0') == std::string::npos;
            };
            const auto& description = asset.description;
            if (!validText(asset.configPath, true) || !validText(asset.sourceSpace, true)
                || !validText(asset.destinationSpace, true) || !validText(asset.look, false)
                || !description.pixelShader || !description.pixelShader->isCompiled()
                || (description.stage != PostProcessStage::BeforeToneMapping
                    && description.stage != PostProcessStage::AfterToneMapping)
                || description.textures.size() > 38 || description.contribution != 1
                || !std::all_of(description.parameters.begin(), description.parameters.end(),
                    [](const float value) { return std::isfinite(value); }))
                return false;
            std::array<bool, D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT> registers{};
            std::array<int, D3D12_COMMONSHADER_SAMPLER_REGISTER_COUNT> samplers;
            samplers.fill(-1);
            samplers[0] = 1;
            for (const auto& texture : description.textures)
            {
                const bool validDimensions = texture.width > 0 && texture.height > 0 && texture.depth > 0
                    && ((texture.dimension == PostProcessOcioTextureDimension::Texture1D
                            && texture.height == 1 && texture.depth == 1
                            && texture.width <= D3D12_REQ_TEXTURE1D_U_DIMENSION)
                        || (texture.dimension == PostProcessOcioTextureDimension::Texture2D
                            && texture.depth == 1
                            && texture.width <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
                            && texture.height <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
                        || (texture.dimension == PostProcessOcioTextureDimension::Texture3D
                            && texture.width <= D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && texture.height <= D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                            && texture.depth <= D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION));
                if (!validDimensions || (texture.channels != 1 && texture.channels != 3)
                    || texture.textureRegister == 0 || texture.textureRegister >= registers.size()
                    || texture.samplerRegister >= samplers.size())
                    return false;
                const auto count = static_cast<std::uint64_t>(texture.width) * texture.height
                    * texture.depth * texture.channels;
                const int linear = texture.linearInterpolation ? 1 : 0;
                if (count != texture.values.size() || registers[texture.textureRegister]
                    || (samplers[texture.samplerRegister] != -1 && samplers[texture.samplerRegister] != linear)
                    || !std::all_of(texture.values.begin(), texture.values.end(),
                        [](const float value) { return std::isfinite(value); }))
                    return false;
                registers[texture.textureRegister] = true;
                samplers[texture.samplerRegister] = linear;
            }
            return true;
        }
    }

    bool savePostProcessOcioAsset(const std::filesystem::path& path, const PostProcessOcioAsset& asset)
    {
        if (path.empty() || !validAsset(asset))
        {
            LOG_ERROR("[OCIO] Cannot save an invalid compiled asset.");
            return false;
        }
        try
        {
            flatbuffers::FlatBufferBuilder builder;
            std::vector<flatbuffers::Offset<Serialization::PostProcessOcioTextureData>> textures;
            textures.reserve(asset.description.textures.size());
            for (const auto& texture : asset.description.textures)
                textures.push_back(Serialization::CreatePostProcessOcioTextureData(builder,
                    static_cast<std::uint8_t>(texture.dimension), texture.width, texture.height, texture.depth,
                    texture.channels, texture.textureRegister, texture.samplerRegister,
                    texture.linearInterpolation, builder.CreateVector(texture.values)));
            const auto bytecode = asset.description.pixelShader->getBytecode();
            const auto shader = builder.CreateVector(static_cast<const std::uint8_t*>(bytecode.pShaderBytecode),
                bytecode.BytecodeLength);
            const auto root = Serialization::CreatePostProcessOcioFile(builder, 1,
                builder.CreateString(asset.configPath), builder.CreateString(asset.sourceSpace),
                builder.CreateString(asset.destinationSpace), builder.CreateString(asset.look),
                static_cast<std::uint8_t>(asset.description.stage), shader, builder.CreateVector(textures),
                builder.CreateVector(asset.description.parameters.data(), asset.description.parameters.size()));
            Serialization::FinishPostProcessOcioFileBuffer(builder, root);
            return Serialization::FlatBufferWriter{}.saveAtomic(path,
                { builder.GetBufferPointer(), builder.GetSize() });
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[OCIO] Cannot allocate compiled asset data.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[OCIO] Compiled asset exceeds available capacity.");
        }
        return false;
    }

    bool loadPostProcessOcioAsset(const std::filesystem::path& path, PostProcessOcioAsset& destination)
    {
        Serialization::FlatBufferReader reader;
        if (!reader.open(path))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!reader.hasIdentifier("POCI") || !Serialization::VerifyPostProcessOcioFileBuffer(verifier))
        {
            LOG_ERROR("[OCIO] Invalid compiled asset.");
            return false;
        }
        const auto* file = Serialization::GetPostProcessOcioFile(reader.data());
        if (file->version() != 1 || file->textures()->size() > 38 || file->parameters()->size() != 16
            || file->stage() > static_cast<std::uint8_t>(PostProcessStage::AfterToneMapping))
        {
            LOG_ERROR("[OCIO] Unsupported compiled asset version or resource count.");
            return false;
        }
        try
        {
            PostProcessOcioAsset candidate;
            std::error_code error;
            candidate.assetPath = std::filesystem::weakly_canonical(path, error);
            if (error || candidate.assetPath.empty())
            {
                LOG_ERROR("[OCIO] Cannot resolve compiled asset path: {}.", error.message());
                return false;
            }
            candidate.configPath = file->config_path()->str();
            candidate.sourceSpace = file->source_space()->str();
            candidate.destinationSpace = file->destination_space()->str();
            candidate.look = file->look()->str();
            candidate.description.stage = static_cast<PostProcessStage>(file->stage());
            std::copy(file->parameters()->begin(), file->parameters()->end(), candidate.description.parameters.begin());
            auto shader = std::make_shared<DX12Shader>();
            if (!shader->loadBytecode({ file->shader()->data(), file->shader()->size() }))
                return false;
            candidate.description.pixelShader = std::move(shader);
            candidate.description.textures.reserve(file->textures()->size());
            for (const auto* texture : *file->textures())
            {
                if (texture->dimension() > static_cast<std::uint8_t>(PostProcessOcioTextureDimension::Texture3D))
                {
                    LOG_ERROR("[OCIO] Invalid compiled LUT dimension.");
                    return false;
                }
                auto& output = candidate.description.textures.emplace_back();
                output.dimension = static_cast<PostProcessOcioTextureDimension>(texture->dimension());
                output.width = texture->width();
                output.height = texture->height();
                output.depth = texture->depth();
                output.channels = texture->channels();
                output.textureRegister = texture->texture_register();
                output.samplerRegister = texture->sampler_register();
                output.linearInterpolation = texture->linear();
                output.values.assign(texture->values()->begin(), texture->values()->end());
            }
            if (!validAsset(candidate))
            {
                LOG_ERROR("[OCIO] Invalid compiled shader, LUT data, or authoring metadata.");
                return false;
            }
            destination = std::move(candidate);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[OCIO] Cannot allocate compiled asset data.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[OCIO] Compiled asset exceeds available capacity.");
        }
        return false;
    }
}
