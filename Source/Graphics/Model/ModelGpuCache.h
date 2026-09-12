#pragma once

#include "Assets\Model\ModelManager.h"
#include "Graphics\DirectX12\Resource.h"
#include "Graphics\Texture\TextureTypes.h"

namespace Engine
{
    class DX12Device;
    class DX12Fence;

    /**
     * @brief 1 Mesh分のGPU BufferとView。
     */
    struct ModelGpuMesh
    {
        DX12UploadBuffer vertexBuffer;               //!< 頂点Buffer
        DX12UploadBuffer indexBuffer;                //!< インデックスBuffer
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{}; //!< 頂点Buffer View
        D3D12_INDEX_BUFFER_VIEW indexBufferView{};   //!< インデックスBuffer View
    };

    /**
     * @brief ModelResourceに対応するGPU描画Resource。
     */
    struct ModelGpuResource
    {
        /**
         * @brief 解決済みの初期Material GPU参照。
         */
        struct Material
        {
            TextureHandle baseColorTexture; //!< Base Color Texture Handle
        };

        std::shared_ptr<const ModelResource> source;       //!< 元のModelResourceへの参照
        std::vector<std::unique_ptr<ModelGpuMesh>> meshes; //!< Mesh単位のGPU描画Resource
        std::vector<Material> materials;                   //!< Material単位のGPU描画Resource
    };

    /**
     * @brief ModelHandle単位でGPU Meshを遅延作成し再利用するCache。
     * @thread_safety Not thread-safe. Render thread only.
     */
    class ModelGpuCache
    {
    public:

        ModelGpuCache() = default;
        ~ModelGpuCache();

        GE_DISABLE_COPY_AND_MOVE(ModelGpuCache);

        /**
         * @brief Cacheを初期化する。
         * @param device DX12Deviceへの参照
         * @param fence DX12Fenceへの参照
         */
        bool initialize(DX12Device& device, DX12Fence& fence) noexcept;

        /**
         * @brief GPU完了を待ってCacheを解放する。
         */
        bool finalize();

        /**
         * @brief Handleに対応するGPU Resourceを取得し、未作成ならUploadする。
         * @param handle ModelHandle
         */
        ModelGpuResource* getOrCreate(ModelHandle handle);

        /**
         * @brief 指定モデルのBufferへ最終使用Fence値を記録する。
         * @param handle ModelHandle
         * @param fenceValue Fence値
         * @return 成功した場合はtrue、それ以外はfalse
         */
        bool markUsed(ModelHandle handle, std::uint64_t fenceValue);

    private:

        /**
         * @brief ModelHandleからユニークなキーを作成する。
         * @param handle ModelHandle
         * @return ユニークなキー
         */
        static std::uint64_t makeKey(ModelHandle handle) noexcept;

        /**
         * @brief ModelHandleに対応するGPU Resourceを作成する。
         * @param handle ModelHandle
         * @return 作成したGPU Resourceへのunique_ptr
         */
        std::unique_ptr<ModelGpuResource> createResource(ModelHandle handle);

        DX12Device* m_device = nullptr; //!< DX12Deviceへの非所有参照
        DX12Fence* m_fence = nullptr;   //!< DX12Fenceへの非所有参照
        std::unordered_map<std::uint64_t, std::unique_ptr<ModelGpuResource>> m_resources; //!< ModelHandleに対応するGPU ResourceのMap
    };
} // namespace Engine