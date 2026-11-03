#pragma once

#include "Assets\Material\MaterialAsset.h"
#include "Graphics\DirectX12\Resource.h"
#include "Graphics\Material\MaterialParameterLayout.h"
#include "Graphics\Material\MaterialShaderVariant.h"
#include "Graphics\Texture\TextureTypes.h"

namespace Engine
{
    /** @brief HLSL terrain layer packing: color followed by metallic/roughness/normalScale/UV repeat. */
    struct TerrainLayerConstants
    {
        Vector4 color = Vector4::One;
        Vector4 parameters = Vector4(0, 1, 1, 1);
    };

    /** @brief Standard property layout remains an unchanged 64-byte prefix. */
    struct MaterialConstantData
    {
        MaterialParameterValues standard;
        Vector4 terrainOptions = Vector4::Zero;
        std::array<TerrainLayerConstants, TERRAIN_LAYER_COUNT> terrainLayers;
    };
    static_assert(offsetof(MaterialConstantData, terrainOptions) == 64);
    static_assert(offsetof(MaterialConstantData, terrainLayers) == 80);
    static_assert(sizeof(MaterialConstantData) == 208);

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
        std::array<TextureHandle, TERRAIN_LAYER_TEXTURE_COUNT> terrainTextures;
        ShaderVariantID shaderVariantID = 0;         //!< Shader Assetが許可した解決済みVariant
        std::uint64_t lastUsedFenceValue = 0;        //!< 最終描画提出Fence値
        bool pendingSubmission = false;             //!< Accessed during preparation but not yet assigned a submission fence.
    };
} // namespace Engine