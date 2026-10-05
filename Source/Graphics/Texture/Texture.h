#pragma once

#include <DirectXTex.h>
#include <memory>
#include "Graphics\DirectX12\Device.h"
#include "Graphics\DirectX12\Descriptor.h"
#include "Graphics\Texture\TextureTypes.h"

namespace Engine
{
    class DX12CommandQueue;
    class DX12Fence;
    class DX12CommandList;

    /**
     * @brief GPU テクスチャリソースの管理クラス
     * @details
     * - DirectX 12 Resource と SRV を所有・管理
     * - ファイルパスなどのメタデータ保持
     * - アップロード後の状態管理
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class Texture
    {
    public:

        /** @brief 未ロードのテクスチャを作成する。 */
        Texture();
        /** @brief Upload Fence完了を確認し、リソースを解放する。 */
        ~Texture();

        GE_DISABLE_COPY_AND_MOVE(Texture);

        /**
         * @brief テクスチャを初期化する（テクスチャデータを GPU に転送）
         * @param device DirectX 12 デバイス
         * @param descriptorHeap SRV 割り当て先の Descriptor Heap
         * @param fence Upload コマンドの完了確認用 Fence
         * @param path テクスチャファイルのパス
         * @param desc テクスチャロード設定
         * @warning fenceはTextureの破棄またはfinalizeまで生存する必要がある
         * @return 初期化に成功した場合は true
         */
        bool initialize(
            ID3D12Device& device,
            DX12DescriptorHeap& descriptorHeap,
            DX12CommandQueue& directQueue,
            DX12Fence& fence,
            const std::filesystem::path& path,
            const TextureLoadDesc& desc = TextureLoadDesc{});

        /**
         * @brief 単色テクスチャを初期化する
         * @param device DirectX 12 デバイス
         * @param descriptorHeap SRV 割り当て先の Descriptor Heap
         * @param fence Upload コマンドの完了確認用 Fence
         * @param color 単色の RGBA 値
         * @param colorSpace 色空間
         * @warning fenceはTextureの破棄またはfinalizeまで生存する必要がある
         * @return 初期化に成功した場合は true
         */
        bool initializeSolidColor(
            ID3D12Device& device,
            DX12DescriptorHeap& descriptorHeap,
            DX12CommandQueue& directQueue,
            DX12Fence& fence,
            const std::array<std::uint8_t, 4>& color,
            TextureColorSpace colorSpace = TextureColorSpace::Linear);

        /**
         * @brief テクスチャリソースを解放する
         * @warning GPU 使用完了を Fence で確認してから呼び出すこと
         * @return GPU完了を確認して解放できた場合はtrue
         */
        bool finalize();

        /**
         * @brief ロード済みかを判定する
         * @return ロード済みの場合は true
         */
        bool isLoaded() const noexcept { return m_state == TextureState::Ready; }

        /**
         * @brief テクスチャの状態を取得する
         * @return テクスチャの状態
         */
        TextureState getState() const noexcept { return m_state; }

        /**
         * @brief GPU リソース情報を取得する
         * @return GPU リソース情報。ロード前または失敗時は nullptr
         */
        const TextureResourceInfo* getResourceInfo() const noexcept;

        /**
         * @brief テクスチャのファイルパスを取得する
         * @return 正規化されたファイルパス
         */
        const std::filesystem::path& getPath() const noexcept { return m_path; }

        /**
         * @brief テクスチャの幅を取得する
         * @return テクスチャ幅（ピクセル）。未初期化時は 0
         */
        uint32_t getWidth() const noexcept { return m_info.width; }

        /**
         * @brief テクスチャの高さを取得する
         * @return テクスチャ高さ（ピクセル）。未初期化時は 0
         */
        uint32_t getHeight() const noexcept { return m_info.height; }

        /**
         * @brief テクスチャのフォーマットを取得する
         * @return DXGI フォーマット。未初期化時は DXGI_FORMAT_UNKNOWN
         */
        DXGI_FORMAT getFormat() const noexcept { return m_info.format; }

        /**
         * @brief テクスチャのミップレベル数を取得する
         * @return ミップレベル数。未初期化時は 1
         */
        uint32_t getMipLevels() const noexcept { return m_info.mipLevels; }

        /**
         * @brief テクスチャの種類を取得する
         * @return テクスチャの種類
         */
        TextureType getType() const noexcept { return m_info.type; }

        /**
         * @brief テクスチャの色空間を取得する
         * @return 色空間
         */
        TextureColorSpace getColorSpace() const noexcept { return m_info.colorSpace; }

        /**
         * @brief 全ミップ・面の画素データ量を取得する（BCブロック単位。GPU割り当て量ではない）
         * @return バイト単位のメモリ使用量。未初期化時は 0
         */
        uint64_t getGPUMemorySize() const noexcept;

    private:

        /**
         * @brief ファイルから画像データを読み込む
         * @details DirectXTex を使用して、拡張子に応じた適切なローダーを呼び出す
         * @param path ファイルパス
         * @return 成功時は DirectXTex::ScratchImage、失敗時は empty
         */
        DirectX::ScratchImage loadImageFromFile(const std::filesystem::path& path);

        /**
         * @brief GPU リソースを作成して Upload する
         * @param device DirectX 12 デバイス
         * @param descriptorHeap Descriptor Heap
         * @param fence Upload 完了用 Fence
         * @param scratchImage アップロード対象の画像
         * @return 成功時は true
         */
        bool createGpuResource(
            ID3D12Device& device,
            DX12DescriptorHeap& descriptorHeap,
            DX12CommandQueue& directQueue,
            DX12Fence& fence,
            const DirectX::ScratchImage& scratchImage);

        /**
         * @brief SRV を作成して Descriptor Heap に割り当てる
         * @param device DirectX 12 デバイス
         * @param descriptorHeap Descriptor Heap
         * @return 成功時は true
         */
        bool createShaderResourceView(ID3D12Device& device, DX12DescriptorHeap& descriptorHeap);

        // リソース
        Microsoft::WRL::ComPtr<ID3D12Resource> m_gpuResource;  //!< GPU テクスチャリソース
        Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadBuffer; //!< Upload 用バッファ（完了後解放）
        std::unique_ptr<DX12CommandList> m_uploadCommandList;  //!< Fence完了までAllocatorとCommand Listを保持

        // メタデータ
        std::filesystem::path m_path;                  //!< ファイルパス（正規化済み）
        TextureResourceInfo m_info;                    //!< GPU リソース情報
        TextureState m_state = TextureState::Unloaded; //!< ロード状態

        // Upload 追跡
        const DX12Fence* m_uploadFence = nullptr; //!< Upload 完了確認用 Fence（非所有）
        uint64_t m_uploadFenceValue = 0;          //!< Upload が完了した Fence 値
    };
} // namespace Engine
