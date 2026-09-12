#pragma once

#include "Core\Math\Geometry.h"
#include "Graphics\Texture\TextureTypes.h"

namespace Engine
{
    /**
     * @brief RenderItemが描画されるパス。
     */
    enum class RenderPassType : std::uint8_t
    {
        DepthOnly,
        Shadow,
        Opaque,
        AlphaTest,
        Transparent,
    };

    /**
     * @brief Rendererが処理する一時的なモデル描画データ。
     * @details GPUリソースを所有せず、1フレーム内だけ有効な参照と描画状態を保持する。
     */
    struct RenderItem
    {
        const D3D12_VERTEX_BUFFER_VIEW* vertexBuffer = nullptr; //!< 非所有の頂点Buffer View
        const D3D12_INDEX_BUFFER_VIEW* indexBuffer = nullptr;   //!< 非所有のIndex Buffer View
        Matrix worldMatrix = Matrix::Identity;                  //!< ObjectのWorld行列
        AABB worldBounds{};                                     //!< Culling用World Bounds
        Vector4 baseColor = Vector4::One;                       //!< 初期Material用BaseColor
        std::uint32_t indexStart = 0;                           //!< Index Buffer内の開始位置
        std::uint32_t indexCount = 0;                           //!< 描画するIndex数
        std::int32_t baseVertex = 0;                            //!< 頂点Indexの基準位置
        std::uint32_t objectID = 0;                             //!< Object識別子
        std::uint32_t pipelineID = 0;                           //!< PSO識別子
        std::uint32_t materialID = 0;                           //!< Material識別子
        std::uint32_t textureID = 0;                            //!< 主Texture識別子
        TextureHandle baseColorTexture;                         //!< BaseColor Texture
        std::uint32_t meshID = 0;                               //!< Mesh識別子
        float cameraDepth = 0.0f;                               //!< Transparent sort用Camera深度
        RenderPassType pass = RenderPassType::Opaque;           //!< 描画Pass
    };
} // namespace Engine