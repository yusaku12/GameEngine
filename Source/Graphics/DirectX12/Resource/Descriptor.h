#pragma once

#include "Core\CoreDefines.h"

namespace Engine
{
    /**
     * @brief CPU から参照する Descriptor Handle
     */
    struct DX12CpuDescriptorHandle
    {
        D3D12_CPU_DESCRIPTOR_HANDLE native{}; //!< DirectX 12 CPU Descriptor Handle
        std::uint32_t index = 0;              //!< Heap 内の Descriptor Index
    };

    /**
     * @brief GPU から参照する Descriptor Handle
     */
    struct DX12GpuDescriptorHandle
    {
        D3D12_GPU_DESCRIPTOR_HANDLE native{}; //!< DirectX 12 GPU Descriptor Handle
        std::uint32_t index = 0;              //!< Heap 内の Descriptor Index
    };

    /**
     * @brief Descriptor Heap の作成設定
     */
    struct DX12DescriptorHeapConfig
    {
        D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; //!< Heap の用途
        std::uint32_t capacity = 0;                                               //!< 割り当て可能な Descriptor 数
        bool shaderVisible = false;                                               //!< GPU から参照可能にするか
    };

    /**
     * @brief CPU/GPU Descriptor Handle の組
     */
    struct DX12DescriptorAllocation
    {
        DX12CpuDescriptorHandle cpu;                //!< CPU Descriptor Handle
        std::optional<DX12GpuDescriptorHandle> gpu; //!< Shader Visible Heap の GPU Descriptor Handle
    };

    /**
     * @brief DirectX 12 Descriptor Heap
     * @details Descriptor は線形に割り当て、解放済みの slot は再利用する。
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class DX12DescriptorHeap
    {
    public:

        DX12DescriptorHeap() = default;
        ~DX12DescriptorHeap() = default;

        GE_DISABLE_COPY_AND_MOVE(DX12DescriptorHeap);

        /**
         * @brief 用途と可視性を指定して Descriptor Heap を作成する
         * @param device Descriptor Heap を作成するデバイス
         * @param config Heap の作成設定
         * @return 作成に成功した場合は true
         */
        bool initialize(ID3D12Device& device, const DX12DescriptorHeapConfig& config);

        /**
         * @brief 保持している Descriptor Heap を解放する
         * @warning GPU が Heap を使用していないことを Fence で確認してから呼び出すこと
         */
        void finalize();

        /**
         * @brief 次の未使用 Descriptor を線形割り当てする
         * @return 割り当てた CPU/GPU Handle。容量不足または未初期化時は std::nullopt
         */
        std::optional<DX12DescriptorAllocation> allocate();

        /**
         * @brief 使用済み Descriptor slot を解放する
         * @param index Heap 内の Descriptor Index
         * @return slot が割り当て済みで、解放できた場合は true
         * @warning GPU が Descriptor を使用していないことを Fence で確認してから呼び出すこと
         */
        bool release(std::uint32_t index);

        /**
         * @brief Shader Visible Heap かを取得する
         * @return Shader Visible Heap の場合は true
         */
        bool isShaderVisible() const noexcept { return m_shaderVisible; }

        /**
         * @brief 割り当て済み Descriptor 数を取得する
         * @return 割り当て済み Descriptor 数
         */
        std::uint32_t getAllocatedCount() const noexcept { return m_allocatedCount; }

        /**
         * @brief Descriptor Heap の総容量を取得する
         * @return Descriptor Heap の総容量
         */
        std::uint32_t getCapacity() const noexcept { return m_capacity; }

        /**
         * @brief DirectX 12 Descriptor Heap を取得する
         * @return 非所有の Descriptor Heap 参照。未初期化時は nullptr
         */
        ID3D12DescriptorHeap* get() const noexcept { return m_heap.Get(); }

    private:

        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap; //!< DirectX 12 Descriptor Heap
        D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart{};            //!< CPU Handle の先頭
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart{};            //!< GPU Handle の先頭
        std::uint32_t m_descriptorSize = 0;                  //!< Descriptor 1 個のサイズ
        std::uint32_t m_capacity = 0;                        //!< 割り当て可能な Descriptor 数
        std::uint32_t m_nextIndex = 0;                       //!< 未使用の新規 Descriptor Index
        std::uint32_t m_allocatedCount = 0;                  //!< 現在割り当て済みの Descriptor 数
        std::vector<std::uint32_t> m_freeIndices;            //!< 再利用可能な Descriptor Index
        std::vector<std::uint8_t> m_allocated;               //!< Index ごとの割り当て状態
        bool m_shaderVisible = false;                        //!< GPU から参照可能か
    };
} // namespace Engine
