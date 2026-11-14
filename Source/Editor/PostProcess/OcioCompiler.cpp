#include "Pch.h"
#include "Editor\PostProcess\OcioCompiler.h"
#include "Core\Containers\String.h"
#include "Core\Logging\Logger.h"
#include "Graphics\Shader\ShaderCompiler.h"
#include <OpenColorIO\OpenColorIO.h>
#include <cctype>
#include <limits>

namespace Engine
{
    namespace
    {
        namespace OCIO = OCIO_NAMESPACE;

        constexpr std::string_view WRAPPER = R"(
cbuffer PostFrame : register(b0) { float4 sourceViewport; float4 targetViewport; uint exposureMode; };
Texture2D<float4> postInput : register(t0);
SamplerState postSourceSampler : register(s0);
SamplerState postPointSampler : register(s1);
)";
        constexpr std::string_view ENTRY = R"(
float4 psMain(float4 position : SV_Position) : SV_Target0
{
    float4 color;
    if (all(sourceViewport == targetViewport))
        color = postInput.Load(int3(int2(position.xy), 0));
    else
    {
        uint width, height;
        postInput.GetDimensions(width, height);
        float2 dimensions = float2(width, height);
        float2 first = clamp(ceil(sourceViewport.xy - 0.5f) + 0.5f, 0.5f, dimensions - 0.5f);
        float2 last = min(dimensions - 0.5f, max(first, floor(sourceViewport.xy + sourceViewport.zw) - 0.5f));
        float2 uv = (position.xy - targetViewport.xy) / targetViewport.zw;
        float2 mapped = clamp(sourceViewport.xy + uv * sourceViewport.zw, first, last);
        color = postInput.SampleLevel(postSourceSampler, mapped / dimensions, 0);
    }
    return postOcioTransform(color);
}
)";

        bool identifier(const std::string_view name)
        {
            if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_'))
                return false;
            return std::all_of(name.begin() + 1, name.end(), [](const char value)
            {
                return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
            });
        }

        bool replaceDeclaration(std::string& text, const std::string& declaration, const std::string& replacement)
        {
            const auto position = text.find(declaration);
            if (position == std::string::npos || text.find(declaration, position + declaration.size()) != std::string::npos)
                return false;
            text.replace(position, declaration.size(), replacement);
            return true;
        }

        void replaceIdentifier(std::string& text, const std::string& name, const std::string_view replacement)
        {
            const auto isIdentifierChar = [](const char value)
            {
                return std::isalnum(static_cast<unsigned char>(value)) || value == '_';
            };
            std::size_t position = 0;
            while ((position = text.find(name, position)) != std::string::npos)
            {
                const std::size_t end = position + name.size();
                if ((position == 0 || !isIdentifierChar(text[position - 1]))
                    && (end == text.size() || !isIdentifierChar(text[end])))
                {
                    text.replace(position, name.size(), replacement);
                    position += replacement.size();
                }
                else
                    position = end;
            }
        }

        bool appendTexture(std::string& shader, PostProcessOcioDescription& description,
            const char* textureName, const char* samplerName, const float* values,
            const PostProcessOcioTextureDimension dimension, const std::uint32_t width,
            const std::uint32_t height, const std::uint32_t depth, const std::uint32_t channels,
            const OCIO::Interpolation interpolation)
        {
            if (!textureName || !samplerName || !values || !identifier(textureName) || !identifier(samplerName)
                || width == 0 || height == 0 || depth == 0 || (channels != 1 && channels != 3)
                || (interpolation != OCIO::INTERP_LINEAR && interpolation != OCIO::INTERP_NEAREST)
                || description.textures.size() >= 38)
                return false;
            if ((dimension == PostProcessOcioTextureDimension::Texture1D
                    && (height != 1 || depth != 1 || width > D3D12_REQ_TEXTURE1D_U_DIMENSION))
                || (dimension == PostProcessOcioTextureDimension::Texture2D
                    && (depth != 1 || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
                        || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION))
                || (dimension == PostProcessOcioTextureDimension::Texture3D
                    && (width > D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                        || height > D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION
                        || depth > D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION)))
                return false;
            const std::uint64_t count = static_cast<std::uint64_t>(width) * height * depth * channels;
            if (count > std::numeric_limits<std::size_t>::max() / sizeof(float))
                return false;
            const std::string texture(textureName);
            const std::string sampler(samplerName);
            const char* const type = dimension == PostProcessOcioTextureDimension::Texture1D ? "Texture1D"
                : dimension == PostProcessOcioTextureDimension::Texture2D ? "Texture2D" : "Texture3D";
            const std::uint32_t slot = static_cast<std::uint32_t>(description.textures.size() + 1);
            const std::string declaration = std::string(type) + " " + texture + ";";
            if (!replaceDeclaration(shader, declaration,
                    std::string(type) + " " + texture + " : register(t" + std::to_string(slot) + ");")
                || !replaceDeclaration(shader, "SamplerState " + sampler + ";", ""))
                return false;
            const bool linear = interpolation == OCIO::INTERP_LINEAR;
            replaceIdentifier(shader, sampler, linear ? "postSourceSampler" : "postPointSampler");
            auto& target = description.textures.emplace_back();
            target.dimension = dimension;
            target.width = width;
            target.height = height;
            target.depth = depth;
            target.channels = channels;
            target.textureRegister = slot;
            target.samplerRegister = linear ? 0 : 1;
            target.linearInterpolation = linear;
            target.values.assign(values, values + static_cast<std::size_t>(count));
            return true;
        }

        struct StagingFiles
        {
            std::filesystem::path source;
            std::filesystem::path bytecode;
            ~StagingFiles()
            {
                for (const auto& path : { source, bytecode })
                {
                    std::error_code error;
                    std::filesystem::remove(path, error);
                    if (error)
                        LOG_ERROR("[OCIO] Cannot remove shader staging file: {}.", error.message());
                }
            }
        };
    }

    bool compilePostProcessOcio(const PostProcessOcioAsset& recipe, PostProcessOcioAsset& destination)
    {
        if (recipe.configPath.empty() || recipe.sourceSpace.empty() || recipe.destinationSpace.empty()
            || recipe.configPath.find('\0') != std::string::npos || recipe.sourceSpace.find('\0') != std::string::npos
            || recipe.destinationSpace.find('\0') != std::string::npos || recipe.look.find('\0') != std::string::npos
            || (recipe.description.stage != PostProcessStage::BeforeToneMapping
                && recipe.description.stage != PostProcessStage::AfterToneMapping))
        {
            LOG_ERROR("[OCIO] Config path, color spaces, look or processing stage is invalid.");
            return false;
        }
        try
        {
            const auto config = OCIO::Config::CreateFromFile(recipe.configPath.c_str());
            config->validate();
            OCIO::ConstProcessorRcPtr processor;
            if (recipe.look.empty())
                processor = config->getProcessor(recipe.sourceSpace.c_str(), recipe.destinationSpace.c_str());
            else
            {
                const auto transform = OCIO::LookTransform::Create();
                transform->setSrc(recipe.sourceSpace.c_str());
                transform->setDst(recipe.destinationSpace.c_str());
                transform->setLooks(recipe.look.c_str());
                processor = config->getProcessor(transform);
            }
            auto shaderDescription = OCIO::GpuShaderDesc::CreateShaderDesc();
            shaderDescription->setLanguage(OCIO::GPU_LANGUAGE_HLSL_SM_5_0);
            shaderDescription->setFunctionName("postOcioTransform");
            shaderDescription->setResourcePrefix("ocio_");
            processor->getDefaultGPUProcessor()->extractGpuShaderInfo(shaderDescription);
            if (shaderDescription->getNumUniforms() != 0)
            {
                LOG_ERROR("[OCIO] Dynamic GPU properties cannot be stored in a static compiled asset.");
                return false;
            }
            std::string shader = shaderDescription->getShaderText();
            PostProcessOcioAsset candidate = recipe;
            candidate.assetPath.clear();
            candidate.description = {};
            candidate.description.stage = recipe.description.stage;
            for (unsigned index = 0; index < shaderDescription->getNumTextures(); ++index)
            {
                const char* textureName = nullptr;
                const char* samplerName = nullptr;
                const float* values = nullptr;
                unsigned width = 0, height = 0;
                OCIO::GpuShaderCreator::TextureType channels;
                OCIO::GpuShaderCreator::TextureDimensions dimension;
                OCIO::Interpolation interpolation;
                shaderDescription->getTexture(index, textureName, samplerName, width, height,
                    channels, dimension, interpolation);
                shaderDescription->getTextureValues(index, values);
                if ((channels != OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL
                        && channels != OCIO::GpuShaderCreator::TEXTURE_RGB_CHANNEL)
                    || (dimension != OCIO::GpuShaderCreator::TEXTURE_1D
                        && dimension != OCIO::GpuShaderCreator::TEXTURE_2D)
                    || !appendTexture(shader, candidate.description, textureName, samplerName, values,
                        dimension == OCIO::GpuShaderCreator::TEXTURE_1D
                            ? PostProcessOcioTextureDimension::Texture1D : PostProcessOcioTextureDimension::Texture2D,
                        width, height, 1, channels == OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL ? 1 : 3,
                        interpolation))
                {
                    LOG_ERROR("[OCIO] Cannot bind a generated 1D/2D LUT.");
                    return false;
                }
            }
            for (unsigned index = 0; index < shaderDescription->getNum3DTextures(); ++index)
            {
                const char* textureName = nullptr;
                const char* samplerName = nullptr;
                const float* values = nullptr;
                unsigned size = 0;
                OCIO::Interpolation interpolation;
                shaderDescription->get3DTexture(index, textureName, samplerName, size, interpolation);
                shaderDescription->get3DTextureValues(index, values);
                if (!appendTexture(shader, candidate.description, textureName, samplerName, values,
                    PostProcessOcioTextureDimension::Texture3D, size, size, size, 3, interpolation))
                {
                    LOG_ERROR("[OCIO] Cannot bind a generated 3D LUT.");
                    return false;
                }
            }
            const auto id = ObjectGUID::generate();
            const auto directory = std::filesystem::current_path() / "Build\\OcioShaderStaging";
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
            {
                LOG_ERROR("[OCIO] Cannot create shader staging directory: {}.", error.message());
                return false;
            }
            const auto name = std::to_wstring(id.high) + L"_" + std::to_wstring(id.low);
            const StagingFiles files{ directory / (name + L".hlsl"), directory / (name + L".cso") };
            std::ofstream source(files.source, std::ios::binary | std::ios::trunc);
            source << WRAPPER << shader << ENTRY;
            source.close();
            if (!source)
            {
                LOG_ERROR("[OCIO] Cannot write generated HLSL.");
                return false;
            }
            const auto result = ShaderCompiler{}.compile({
                .sourcePath = files.source,
                .outputPath = files.bytecode,
                .entryPoint = "psMain",
                .stage = ShaderStage::Pixel,
            });
            if (!result.success)
            {
                LOG_ERROR("[OCIO] Generated HLSL compilation failed:\n{}", result.diagnostics);
                return false;
            }
            auto bytecode = std::make_shared<DX12Shader>();
            if (!bytecode->load({ files.bytecode }))
                return false;
            candidate.description.pixelShader = std::move(bytecode);
            destination = std::move(candidate);
            return true;
        }
        catch (const OCIO::Exception& error)
        {
            LOG_ERROR("[OCIO] Config or transform error: {}.", error.what());
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[OCIO] Cannot allocate shader generation resources.");
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            LOG_ERROR("[OCIO] Shader staging failed: {}.", error.what());
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[OCIO] Generated shader or LUT exceeds available capacity.");
        }
        return false;
    }
}
