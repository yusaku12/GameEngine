#pragma once

#include "Assets\Material\MaterialAsset.h"
#include "Graphics\DirectX12\Resource.h"
#include "Graphics\Material\MaterialParameterLayout.h"
#include "Graphics\Material\MaterialShaderVariant.h"
#include "Graphics\Texture\TextureTypes.h"

namespace Engine
{
    using MaterialConstantData = MaterialParameterValues;

    /**
     * @brief Material Assetに対応するGPU描画表現。
     */
    struct MaterialGpuResource
    {
        MaterialHandle handle;                       //!< Cache内でのMaterial Handle
        std::shared_ptr<const MaterialAsset> source; //!< 不変Material Asset Snapshot
        DX12UploadBuffer constantBuffer;             //!< 256-byte境界で確保したMaterial Constants
        TextureHandle baseColorTexture;              //!< sRGB Base Color
        TextureHandle normalTexture;                 //!< Linear Normal
        TextureHandle metallicRoughnessTexture;      //!< Linear Metallic-Roughness
        TextureHandle ambientOcclusionTexture;       //!< Linear Ambient Occlusion
        TextureHandle emissiveTexture;               //!< sRGB Emissive
        ShaderVariantID shaderVariantID = 0;         //!< Shader Assetが許可した解決済みVariant
        std::uint64_t lastUsedFenceValue = 0;        //!< 最終描画提出Fence値
    };
} // namespace Engine