#pragma once

#include "Graphics\DirectX12\Resource\Descriptor.h"
#include "Graphics\DirectX12\Resource\Resource.h"

namespace Engine
{
    /** @brief PostEffectの借用画像。所有者とDescriptorはフレーム提出完了まで生存すること。 */
    struct PostProcessImage
    {
        DX12Resource* resource = nullptr;                 //!< Non-owning texture.
        DX12GpuDescriptorHandle srv{};                   //!< Shader-visible SRV.
        DX12CpuDescriptorHandle rtv{};                   //!< Render target view.
        DX12GpuDescriptorHandle uav{};                   //!< Optional shader-visible UAV.
    };

    /** @brief 解像度と色精度が一致する一時画像の設定。 */
    struct PostProcessTargetDescription
    {
        std::uint32_t width = 0; //!< Pixel width.
        std::uint32_t height = 0; //!< Pixel height.
        DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT; //!< Linear color format.
        bool unorderedAccess = false; //!< Create a typed UAV for compute output.
        bool operator==(const PostProcessTargetDescription&) const = default;
    };

    /**
     * @brief Fence付き一時RTV/SRV Textureと任意のUAVの再利用Pool。
     * @details acquire/releaseは同じコマンド列内の論理寿命。GPU寿命はmarkSubmittedで追跡する。
     * @thread_safety Render thread only. The device, fence and SRV heap must outlive this pool.
     */
    class PostProcessTargetPool
    {
    public:
        PostProcessTargetPool() = default;
        ~PostProcessTargetPool();
        GE_DISABLE_COPY_AND_MOVE(PostProcessTargetPool);

        /** @brief 既存Device/Fence/SRV Heapへ接続する。成功時true。 */
        bool initialize(ID3D12Device& device, const DX12Fence& fence, DX12DescriptorHeap& srvHeap);
        /** @brief GPU完了を待ち、TextureとDescriptorを解放する。成功時true。 */
        bool finalize();
        /** @brief 前回の記録が提出済みであることを検証し、新しい記録を開始する。 */
        bool beginRecording();
        /** @brief 指定画像を借用する。失敗時nullptrとログ。返却まで同じ画像を再貸出しない。 */
        PostProcessImage* acquire(const PostProcessTargetDescription& description);
        /** @brief 借用を返却する。同じコマンド列内で再利用可能にする。 */
        bool release(const PostProcessImage& image);
        /** @brief 記録中に使った画像へ提出Fenceを関連付ける。 */
        bool markSubmitted(std::uint64_t fenceValue);
        /** @brief 記録中でないときGPU完了を待って一時画像だけを解放する。 */
        bool clear();
        /** @brief 現在保持しているTexture数。 */
        std::size_t size() const noexcept { return m_targets.size(); }

    private:
        struct Target
        {
            DX12Resource resource;
            PostProcessImage image;
            PostProcessTargetDescription description;
            std::optional<DX12DescriptorAllocation> srv;
            std::optional<DX12DescriptorAllocation> uav;
            DX12DescriptorHeap rtvHeap;
            std::uint64_t fenceValue = 0;
            bool borrowed = false;
            bool touched = false;
        };
        bool releaseTargets();
        ID3D12Device* m_device = nullptr;
        const DX12Fence* m_fence = nullptr;
        DX12DescriptorHeap* m_srvHeap = nullptr;
        std::vector<std::unique_ptr<Target>> m_targets;
        bool m_recording = false;
    };
}
