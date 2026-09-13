#pragma once

#include "Assets\Material\MaterialManager.h"
#include "Core\CoreDefines.h"
#include "Graphics\Material\MaterialGpuResource.h"

namespace Engine
{
    class DX12Device;
    class DX12Fence;

    /**
     * @brief MaterialHandle単位でGPU描画Resourceを遅延作成するCache。
     * @thread_safety Not thread-safe. Render thread only.
     */
    class MaterialGpuCache
    {
    public:

        MaterialGpuCache() = default;
        ~MaterialGpuCache();

        GE_DISABLE_COPY_AND_MOVE(MaterialGpuCache);

        /**
         * @brief Cacheを初期化する。
         * @param device DX12Device参照
         * @param fence DX12Fence参照
         */
        bool initialize(DX12Device& device, DX12Fence& fence) noexcept;

        /**
         * @brief GPU完了を待ってCacheを解放する。
         */
        bool finalize();

        /**
         * @brief Material GPU Resourceを取得し、未作成なら遅延作成する。失敗時はError MaterialへFallbackする。
         * @param handle MaterialHandle
         */
        MaterialGpuResource* getOrCreate(MaterialHandle handle);

        /**
         * @brief Material Bufferへ最終使用Fence値を記録する。
         * @param handle MaterialHandle
         * @param fenceValue 最終使用Fence値
         * @return 成功した場合はtrue、失敗した場合はfalse
         */
        bool markUsed(MaterialHandle handle, std::uint64_t fenceValue);

        /**
         * @brief Cache済みMaterialを新しいShader Layout向けに再生成する。
         * @return 全Resourceを生成して交換できた場合はtrue。失敗時は現在のCacheを維持する。
         */
        bool rebuildAll();

        /**
         * @brief unload済みMaterialのGPU ResourceをFence完了後に破棄する。
         */
        void collectGarbage();

    private:

        /**
         * @brief MaterialHandleからCache用のユニークキーを生成する。
         * @param handle MaterialHandle
         * @return ユニークキー
         */
        static std::uint64_t makeKey(MaterialHandle handle) noexcept;

        /**
         * @brief MaterialHandleからMaterialGpuResourceを作成する。
         * @param handle MaterialHandle
         * @return 作成に成功した場合はMaterialGpuResourceのunique_ptr、失敗時はnullptr
         */
        std::unique_ptr<MaterialGpuResource> createResource(MaterialHandle handle);

        DX12Device* m_device = nullptr;                                                      //!< DX12Device参照
        DX12Fence* m_fence = nullptr;                                                        //!< DX12Fence参照
        std::unordered_map<std::uint64_t, std::unique_ptr<MaterialGpuResource>> m_resources; //!< MaterialHandleユニークキー -> MaterialGpuResource
    };
} // namespace Engine