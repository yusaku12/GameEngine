#pragma once

#include <cstdint>
#include <dxgi.h>

namespace Engine
{
    /**
     * @brief テクスチャハンドル
     * @details リソースの寿命管理とキャッシュ機構のため、生ポインタではなくハンドルを使用する
     */
    struct TextureHandle
    {
        static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF; //!< 無効なインデックス値

        uint32_t index = INVALID_INDEX; //!< テクスチャ管理配列内のインデックス

        /**
         * @brief 有効なハンドルか判定する
         * @return 有効な場合は true
         */
        bool isValid() const noexcept { return index != INVALID_INDEX; }

        /**
         * @brief 無効なハンドルを作成する
         * @return 無効なハンドル
         */
        static constexpr TextureHandle Invalid() noexcept { return TextureHandle{ INVALID_INDEX }; }

        /**
         * @brief ハンドル同値性チェック
         */
        bool operator==(const TextureHandle& other) const noexcept { return index == other.index; }
        bool operator!=(const TextureHandle& other) const noexcept { return !(*this == other); }
    };

    /**
     * @brief テクスチャの色空間
     * @details sRGB と Linear の自動判定、または手動指定
     */
    enum class TextureColorSpace : uint8_t
    {
        Auto,    //!< 拡張子などから自動判定（.png, .jpg は sRGB、.dds は DDS内の情報から判定）
        Linear,  //!< Linear (Normal, Roughness, Metallic, AO, Mask等)
        SRGB,    //!< sRGB (Albedo, BaseColor, Diffuse等)
    };

    /**
     * @brief テクスチャのロード設定
     */
    struct TextureLoadDesc
    {
        TextureColorSpace colorSpace = TextureColorSpace::Auto; //!< 色空間の指定
        bool generateMips = true;                               //!< ミップマップを生成するか
        bool allowCompression = true;                           //!< 圧縮テクスチャをそのまま使用するか
        bool premultipliedAlpha = false;                        //!< 事前乗算アルファか
    };

    /**
     * @brief テクスチャの種類
     * @details 現時点では Texture2D のサポートを優先。将来的な拡張に対応した設計
     */
    enum class TextureType : uint8_t
    {
        Texture2D,      //!< 2D テクスチャ
        TextureCube,    //!< キューブマップ（IBL等で使用）
        Texture2DArray, //!< 2D テクスチャ配列
        Texture3D,      //!< 3D テクスチャ
    };

    /**
     * @brief テクスチャリソースの状態
     * @details 非同期ロード拡張に向けた設計
     */
    enum class TextureState : uint8_t
    {
        Unloaded,  //!< ロード前
        Loading,   //!< ロード中
        Uploading, //!< GPU へのアップロード中
        Ready,     //!< 使用可能
        Failed,    //!< ロード失敗
    };

    /**
     * @brief テクスチャの GPU リソース情報
     * @details Texture から Shader へデータを渡すための情報
     */
    struct TextureResourceInfo
    {
        ID3D12Resource* resource = nullptr;                       //!< GPU リソース
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};                        //!< Shader Resource View
        uint32_t srvIndex = UINT32_MAX;                           //!< SRV のディスクリプタインデックス（Bindless用）
        uint32_t width = 0;                                       //!< テクスチャ幅
        uint32_t height = 0;                                      //!< テクスチャ高さ
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;                 //!< テクスチャフォーマット
        uint32_t mipLevels = 1;                                   //!< ミップレベル数
        TextureType type = TextureType::Texture2D;                //!< テクスチャの種類
        TextureColorSpace colorSpace = TextureColorSpace::Linear; //!< 色空間
    };
} // namespace Engine
