#include "Pch.h"
#include "Assets\PostProcess\Serialization\PostProcessEffectSerializer.h"
#include "Assets\PostProcess\PostProcessEffectAssetManager.h"
#include "Core\Containers\String.h"
#include "Core\Serialization\FlatBufferReader.h"
#include "Core\Serialization\FlatBufferWriter.h"
#include "Core\Serialization\SerializationVersions.h"
#include "Generated\FlatBuffers\PostProcessEffectAsset_generated.h"
#include <cmath>
#include "Core\Logging\Logger.h"
#include "Core\Math\MathFunction.h"

namespace Engine::Serialization
{
    namespace
    {
        std::string toUtf8(const std::filesystem::path& path)
        {
            const auto text = String::fromWide(path.c_str());
            return text.size() == 0 ? std::string{} : std::string(text.c_str(), text.size());
        }

        bool validAsset(const Engine::PostProcessEffectAsset& asset)
        {
            if (!asset.guid.isValid() || asset.name.empty() || asset.name.find('\0') != std::string::npos
                || asset.sourcePath.empty() || asset.entryPoint.empty() || asset.entryPoint.find('\0') != std::string::npos
                || asset.compiledBytecodeFile.empty() || asset.compiledBytecodeFile.find('\0') != std::string::npos
                || std::filesystem::path(asset.compiledBytecodeFile).filename() != asset.compiledBytecodeFile
                || asset.stage > PostProcessStage::AfterToneMapping || asset.resolutionDivisor == 0
                || asset.resolutionDivisor > 16 || asset.parameters.size() > 16)
            {
                LOG_ERROR("[PostProcessEffect] Invalid asset identity, source, stage, resolution, or parameter count.");
                return false;
            }
            for (const auto& parameter : asset.parameters)
                if (parameter.name.empty() || parameter.name.find('\0') != std::string::npos
                    || !std::isfinite(parameter.defaultValue) || !std::isfinite(parameter.minimum)
                    || !std::isfinite(parameter.maximum) || parameter.minimum > parameter.maximum
                    || parameter.defaultValue < parameter.minimum || parameter.defaultValue > parameter.maximum)
                {
                    LOG_ERROR("[PostProcessEffect] Invalid parameter metadata '{}'.", parameter.name);
                    return false;
                }
            return true;
        }
    }

    bool PostProcessEffectSerializer::save(const std::filesystem::path& path,
        const Engine::PostProcessEffectAsset& asset) const
    {
        if (!validAsset(asset))
            return false;
        const std::string sourcePath = toUtf8(asset.sourcePath);
        if (sourcePath.empty() || sourcePath.find('\0') != std::string::npos)
        {
            LOG_ERROR("[PostProcessEffect] Cannot encode the HLSL source path as UTF-8.");
            return false;
        }
        flatbuffers::FlatBufferBuilder builder(512);
        const AssetGuid guid(asset.guid.high, asset.guid.low);
        std::vector<flatbuffers::Offset<PostProcessEffectParameterData>> parameters;
        parameters.reserve(asset.parameters.size());
        for (const auto& parameter : asset.parameters)
            parameters.push_back(CreatePostProcessEffectParameterData(builder,
                builder.CreateString(parameter.name), parameter.defaultValue, parameter.minimum, parameter.maximum));
        const auto data = CreatePostProcessEffectAssetData(builder, &guid, builder.CreateString(asset.name),
            builder.CreateString(sourcePath), builder.CreateString(asset.compiledBytecodeFile),
            builder.CreateString(asset.entryPoint),
            static_cast<PostProcessStageData>(asset.stage), asset.resolutionDivisor,
            builder.CreateVector(parameters));
        const auto header = CreateFileHeader(builder, CURRENT_SCHEMA_VERSION, CURRENT_POST_PROCESS_EFFECT_ASSET_VERSION, 0);
        FinishPostProcessEffectAssetFileBuffer(builder, CreatePostProcessEffectAssetFile(builder, header, data));
        return FlatBufferWriter{}.saveAtomic(path, { builder.GetBufferPointer(), builder.GetSize() });
    }

    bool PostProcessEffectSerializer::load(const std::filesystem::path& path,
        Engine::PostProcessEffectAsset& asset) const
    {
        FlatBufferReader reader;
        if (!reader.open(path))
            return false;
        flatbuffers::Verifier verifier(reader.data(), reader.size());
        if (!reader.hasIdentifier("PPEA") || !VerifyPostProcessEffectAssetFileBuffer(verifier))
        {
            LOG_ERROR("[PostProcessEffect] Invalid asset identifier or FlatBuffers data: {}.", toUtf8(path));
            return false;
        }
        const auto* file = GetPostProcessEffectAssetFile(reader.data());
        const auto* source = file->asset();
        if (file->header()->schema_version() != CURRENT_SCHEMA_VERSION
            || file->header()->asset_version() != CURRENT_POST_PROCESS_EFFECT_ASSET_VERSION
            || source->guid() == nullptr || source->name()->size() == 0 || source->source_path()->size() == 0
            || source->compiled_bytecode_file()->size() == 0 || source->entry_point()->size() == 0
            || source->stage() > PostProcessStageData_AfterToneMapping
            || source->parameters() == nullptr || source->parameters()->size() > 16)
        {
            LOG_ERROR("[PostProcessEffect] Unsupported version or missing required asset fields: {}.", toUtf8(path));
            return false;
        }
        Engine::PostProcessEffectAsset loaded;
        loaded.guid = { source->guid()->high(), source->guid()->low() };
        loaded.name = source->name()->str();
        loaded.sourcePath = std::filesystem::path(String(source->source_path()->str()).toWide());
        loaded.compiledBytecodeFile = source->compiled_bytecode_file()->str();
        loaded.entryPoint = source->entry_point()->str();
        loaded.stage = static_cast<Engine::PostProcessStage>(source->stage());
        loaded.resolutionDivisor = source->resolution_divisor();
        if (!loaded.guid.isValid() || loaded.name.find('\0') != std::string::npos
            || loaded.entryPoint.find('\0') != std::string::npos || loaded.sourcePath.empty()
            || loaded.compiledBytecodeFile.find('\0') != std::string::npos
            || std::filesystem::path(loaded.compiledBytecodeFile).filename() != loaded.compiledBytecodeFile)
        {
            LOG_ERROR("[PostProcessEffect] Invalid asset identity or UTF-8 path: {}.", toUtf8(path));
            return false;
        }
        loaded.parameters.reserve(source->parameters()->size());
        for (const auto* parameter : *source->parameters())
        {
            if (parameter == nullptr || parameter->name() == nullptr)
                return false;
            loaded.parameters.push_back({ parameter->name()->str(), parameter->default_value(),
                parameter->minimum(), parameter->maximum() });
        }
        if (!validAsset(loaded))
            return false;
        asset = std::move(loaded);
        return true;
    }

    bool writePostProcessEffectReference(const PostProcessEffectHandle handle,
        PostProcessEffectReference& destination)
    {
        PostProcessEffectReference candidate;
        if (handle.isValid())
        {
            auto& manager = PostProcessEffectAssetManager::instance();
            const auto asset = manager.get(handle);
            const auto path = manager.getPath(handle);
            if (!asset || path.empty())
            {
                LOG_ERROR("[PostProcessEffect] Save the referenced effect asset before saving its Profile.");
                return false;
            }
            candidate.guid = asset->guid;
            candidate.path = toUtf8(path);
            if (candidate.path.empty())
            {
                LOG_ERROR("[PostProcessEffect] Cannot encode the asset reference path as UTF-8.");
                return false;
            }
        }
        destination = std::move(candidate);
        return true;
    }

    bool readPostProcessEffectReference(const PostProcessEffectReference& reference,
        PostProcessEffectHandle& destination)
    {
        if (reference.path.find('\0') != std::string::npos
            || reference.guid.isValid() != !reference.path.empty())
        {
            LOG_ERROR("[PostProcessEffect] Invalid effect GUID/path pair.");
            return false;
        }
        PostProcessEffectHandle candidate;
        if (reference.guid.isValid())
        {
            const auto wide = String(reference.path).toWide();
            if (wide.empty())
            {
                LOG_ERROR("[PostProcessEffect] Invalid UTF-8 effect asset path.");
                return false;
            }
            candidate = PostProcessEffectAssetManager::instance().load(std::filesystem::path(wide), reference.guid);
            if (!candidate.isValid())
                return false;
        }
        destination = candidate;
        return true;
    }
}
