#pragma once

#include "Graphics\Texture\TextureTypes.h"
#include "Graphics\Texture\Texture.h"
#include "Graphics\DirectX12\Descriptor.h"

namespace Engine
{
    class DX12Device;
    class DX12CommandQueue;
    class DX12Fence;

    /**
     * @brief テクスチャの一元管理クラス
     * @details
     * - テクスチャのロード・キャッシング
     * - テクスチャの生存期間管理
     * - デフォルトテクスチャの提供
     * - SRV ディスクリプタヒープの管理
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class TextureManager
    {
    public:

        /**
         * @brief TextureManager を取得する（シングルトン）
         * @return TextureManager インスタンスへの参照
         */
        static TextureManager& instance() noexcept
        {
            static TextureManager instance;
            return instance;
        }

        GE_DISABLE_COPY_AND_MOVE(TextureManager);

        /**
         * @brief テクスチャマネージャーを初期化する
         * @param device DirectX 12 デバイス
         * @param directQueue コマンドキュー
         * @param directFence Fence（Upload コマンド追跡用）
         * @param descriptorHeapCapacity SRV ディスクリプタヒープの容量（デフォルト: 1000）
         * @return 初期化に成功した場合は true
         */
        bool initialize(
            DX12Device& device,
            DX12CommandQueue& directQueue,
            DX12Fence& directFence,
            uint32_t descriptorHeapCapacity = 1000);

        /**
         * @brief テクスチャマネージャーを終了する
         * @return 終了処理に成功した場合は true
         */
        bool finalize();

        /**
         * @brief テクスチャをロードする
         * @param path テクスチャファイルのパス
         * @return 割り当てたテクスチャハンドル。失敗時は無効なハンドル
         */
        TextureHandle load(const std::filesystem::path& path)
        {
            return load(path, TextureLoadDesc{});
        }

        /**
         * @brief テクスチャをロードする（カスタム設定）
         * @param path テクスチャファイルのパス
         * @param desc ロード設定
         * @return 割り当てたテクスチャハンドル。失敗時は無効なハンドル
         */
        TextureHandle load(
            const std::filesystem::path& path,
            const TextureLoadDesc& desc);

        /**
         * @brief テクスチャを取得する
         * @param handle テクスチャハンドル
         * @return テクスチャへのポインタ。無効なハンドル時は nullptr
         */
        Texture* get(TextureHandle handle) noexcept;

        /**
         * @brief テクスチャを取得する（const）
         * @param handle テクスチャハンドル
         * @return テクスチャへのポインタ。無効なハンドル時は nullptr
         */
        const Texture* get(TextureHandle handle) const noexcept;

        /**
         * @brief テクスチャをアンロードする
         * @param handle テクスチャハンドル
         */
        void unload(TextureHandle handle) noexcept;

        /**
         * @brief 全テクスチャをクリアする
         * @warning フレーム中途でのクリアは避けること（GPU リソース使用中の場合あり）
         */
        void clear() noexcept;

        /**
         * @brief パスが既にロード済みか確認する
         * @param path テクスチャファイルのパス
         * @return ロード済みの場合は true
         */
        bool exists(const std::filesystem::path& path) const noexcept;

        /** @brief TextureHandleに対応するBindless用SRV indexを取得する */
        std::uint32_t getSRVIndex(TextureHandle handle) const noexcept;

        /**
         * @brief ロード済みテクスチャ数を取得する
         * @return テクスチャ数
         */
        uint32_t getLoadedTextureCount() const noexcept { return m_nextHandle; }

        /**
         * @brief 白いデフォルトテクスチャを取得する
         * @details ロード失敗時や初期化時に使用するフォールバック
         * @return テクスチャハンドル
         */
        TextureHandle getWhiteTexture() const noexcept { return m_defaultWhite; }

        /**
         * @brief 黒いデフォルトテクスチャを取得する
         * @return テクスチャハンドル
         */
        TextureHandle getBlackTexture() const noexcept { return m_defaultBlack; }

        /**
         * @brief 法線マップ用デフォルトテクスチャを取得する（青紫色）
         * @return テクスチャハンドル
         */
        TextureHandle getNormalTexture() const noexcept { return m_defaultNormal; }

        /**
         * @brief エラー用デフォルトテクスチャを取得する（マゼンタ色）
         * @return テクスチャハンドル
         */
        TextureHandle getErrorTexture() const noexcept { return m_defaultError; }

        /**
         * @brief SRV ディスクリプタヒープを取得する
         * @return Descriptor Heap への参照
         */
        DX12DescriptorHeap& getDescriptorHeap() noexcept { return m_descriptorHeap; }

        /**
         * @brief SRV ディスクリプタヒープを取得する（const）
         * @return Descriptor Heap への参照
         */
        const DX12DescriptorHeap& getDescriptorHeap() const noexcept { return m_descriptorHeap; }

    private:

        TextureManager() = default;
        ~TextureManager() = default;

        /**
         * @brief デフォルトテクスチャを作成する
         * @return 成功時は true
         */
        bool createDefaultTextures();

        /**
         * @brief テクスチャを実際にロードする（キャッシュ処理なし）
         * @param path 正規化されたファイルパス
         * @param desc ロード設定
         * @return 成功時はハンドル、失敗時は無効なハンドル
         */
        TextureHandle loadInternal(const std::filesystem::path& path, const TextureLoadDesc& desc);

        // リソース
        DX12Device* m_device = nullptr;            //!< DirectX 12 デバイス（非所有）
        DX12CommandQueue* m_directQueue = nullptr; //!< コマンドキュー（非所有）
        DX12Fence* m_directFence = nullptr;        //!< Fence（非所有）
        DX12DescriptorHeap m_descriptorHeap;       //!< SRV ディスクリプタヒープ

        // テクスチャストレージ
        std::vector<std::unique_ptr<Texture>> m_textures; //!< テクスチャ配列
        uint32_t m_nextHandle = 0;                        //!< 次に割り当てるハンドルインデックス

        // キャッシュ機構
        std::unordered_map<std::string, uint32_t> m_pathToHandle; //!< パス → ハンドルインデックスのマッピング

        // デフォルトテクスチャ
        TextureHandle m_defaultWhite;  //!< White (1.0, 1.0, 1.0, 1.0)
        TextureHandle m_defaultBlack;  //!< Black (0.0, 0.0, 0.0, 1.0)
        TextureHandle m_defaultNormal; //!< Normal (0.5, 0.5, 1.0, 1.0) - 青紫
        TextureHandle m_defaultError;  //!< Error (1.0, 0.0, 1.0, 1.0) - マゼンタ
    };
} // namespace Engine
