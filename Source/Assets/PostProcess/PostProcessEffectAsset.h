#pragma once

#include "Core\Object\ObjectGUID.h"
#include "Graphics\PostProcess\PostProcessSettings.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Engine
{
    class DX12Shader;

    /** @brief Scalar constant metadata exposed to the PostProcess Profile Inspector. */
    struct PostProcessEffectParameter
    {
        std::string name;
        float defaultValue = 0;
        float minimum = 0;
        float maximum = 1;
        bool operator==(const PostProcessEffectParameter&) const = default;
    };

    /** @brief User-authored shader definition plus its immutable compiled bytecode snapshot. */
    struct PostProcessEffectAsset
    {
        ObjectGUID guid;
        std::string name = "Custom Post Effect";
        std::filesystem::path sourcePath;
        std::string compiledBytecodeFile;
        std::string entryPoint = "psMain";
        PostProcessStage stage = PostProcessStage::BeforeToneMapping;
        std::uint32_t resolutionDivisor = 1;
        std::vector<PostProcessEffectParameter> parameters;
        std::shared_ptr<const DX12Shader> pixelShader;
    };

    /** @brief Generation-checked non-owning handle to a registered effect asset. */
    struct PostProcessEffectHandle
    {
        std::uint32_t index = UINT32_MAX;
        std::uint32_t generation = 0;
        bool isValid() const noexcept { return index != UINT32_MAX && generation != 0; }
        bool operator==(const PostProcessEffectHandle&) const = default;
    };
}
