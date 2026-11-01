#pragma once

#include "Graphics\DirectX12\Descriptor.h"
#include "Graphics\DirectX12\Resource.h"
#include "Core\Math\MathTypes.h"

namespace Engine
{
    class DX12ComputePipeline;
    class DX12Shader;
    class RenderQueue;

    /** @brief Fence完了済みの色描画統計。インスタンスはSubMesh単位で数える。 */
    struct GpuOcclusionStatistics
    {
        std::uint32_t batches = 0;
        std::uint32_t occludedBatches = 0;
        std::uint32_t instances = 0;
        std::uint32_t occludedInstances = 0;
    };

    /**
     * @brief 同フレームHi-Zで色描画Instanceを圧縮し、Indirect DrawとCommand Countを生成する。
     * @details Depth/Shadow/Transparentは除外しない。検証済みPose BoundsがあればAnimationも判定する。
     * 結果のCPU回収は統計にのみ使用する。
     * @thread_safety Mutation is render-thread only. execute supports distinct Command Lists concurrently after record.
     * Frame slotの再利用・解放前にGPU Fence完了とCommand recording jobsのjoinが必要。
     */
    class DX12GpuOcclusionCulling
    {
    public:
        GE_DISABLE_COPY_AND_MOVE(DX12GpuOcclusionCulling);
        DX12GpuOcclusionCulling() = default;

        /**
         * @brief Hi-ZとCull ShaderのRoot layoutに対応するPipelineを作成する。
         * @param device 作成先。
         * @param depthShader Hi-Z生成Compute Shader。
         * @param cullShader 可視判定Compute Shader。
         * @param depthPipeline 未初期化のHi-Z Pipeline。
         * @param cullPipeline 未初期化のCull Pipeline。
         * @return 作成成功時true。
         */
        static bool createPipelines(ID3D12Device& device, const DX12Shader& depthShader,
            const DX12Shader& cullShader, DX12ComputePipeline& depthPipeline, DX12ComputePipeline& cullPipeline);

        /**
         * @brief 完了済みGPU結果を統計として回収し、Frame記録状態をリセットする。
         * @param statistics 回収先。未提出Frameでは0。
         * @return 回収成功時true。
         */
        bool collectCompletedResults(GpuOcclusionStatistics& statistics);

        /**
         * @brief Depth Pre-Pass後、Color Pass前にHi-Z生成と可視Instance圧縮を記録する。
         * @param commandList 記録中のDirect Command List。
         * @param device Resource作成用Device。
         * @param fence Frame完了を追跡するFence。
         * @param depth 同フレームのR32_TYPELESS Depth Target。終了時はDEPTH_WRITEに戻す。
         * @param depthPipeline Hi-Z生成Pipeline。
         * @param cullPipeline 可視判定Pipeline。
         * @param queue Rendererと同じソート済みQueue。
         * @param instances Queue Index順に並んだ128-byte Instance Constants。
         * @param viewProjection CameraのView Projection。
         * @param viewport Depth Target内のCamera Viewport。
         * @param instancing Rendererと同じバッチ分割設定。
         * @return 記録成功時true。
         */
        bool record(DX12CommandList& commandList, ID3D12Device& device, const DX12Fence& fence,
            DX12Resource& depth, const DX12ComputePipeline& depthPipeline, const DX12ComputePipeline& cullPipeline,
            const RenderQueue& queue, ID3D12Resource& instances, const Matrix& viewProjection,
            const D3D12_VIEWPORT& viewport, bool instancing);

        /**
         * @brief 圧縮Instanceを設定し、Count Buffer付きIndirect Drawを記録する。
         * @param commandList 記録先。対応するModel Root Signatureを設定済みであること。
         * @param itemIndex バッチ先頭のQueue Index。
         * @return 対応バッチを実行した場合true。非対象バッチはfalseで呼び出し元がDirect Drawする。
         */
        bool execute(ID3D12GraphicsCommandList& commandList, std::size_t itemIndex) const noexcept;

        /**
         * @brief GPU提出後にUpload/ReadbackのFence値を記録する。
         * @param fenceValue 提出Fence値。
         * @return 記録成功時true。
         */
        bool markSubmitted(std::uint64_t fenceValue);

        /** @brief GPU完了後に全Resourceを解放する。 @return 解放成功時true。 */
        bool finalize();

        /** @brief 現在Frameの色描画候補バッチ数。 @return バッチ数。 */
        std::uint32_t getBatchCount() const noexcept { return static_cast<std::uint32_t>(m_batches.size()); }

    private:
        struct Candidate
        {
            Vector3 center;
            std::uint32_t canTest = 0;
            Vector3 extents;
            std::uint32_t padding = 0;
        };
        struct Batch
        {
            std::uint32_t first = 0;
            std::uint32_t instanceCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t indexStart = 0;
            std::int32_t baseVertex = 0;
        };
        struct Command
        {
            D3D12_DRAW_INDEXED_ARGUMENTS draw;
            std::uint32_t count = 0;
        };
        static_assert(sizeof(Candidate) == 32);
        static_assert(sizeof(Batch) == 20);
        static_assert(sizeof(Command) == 24 && offsetof(Command, count) == 20);
        static constexpr std::uint32_t INSTANCE_STRIDE = 128;

        bool ensureCapacity(ID3D12Device& device, const DX12Fence& fence, std::uint32_t itemCount);
        bool ensureHierarchy(ID3D12Device& device, DX12Resource& depth);
        bool buildHierarchy(DX12CommandList& commandList, DX12Resource& depth, const DX12ComputePipeline& pipeline);

        Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_signature;
        DX12DescriptorHeap m_heap;
        DX12Resource m_hierarchy;
        DX12Resource m_visibleInstances;
        DX12Resource m_commands;
        DX12UploadBuffer m_candidateUpload;
        DX12UploadBuffer m_batchUpload;
        DX12ReadbackBuffer m_readback;
        std::vector<Candidate> m_candidates;
        std::vector<Batch> m_batches;
        std::vector<std::uint32_t> m_itemBatches;
        std::vector<Command> m_completedCommands;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> m_mipSources;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> m_mipTargets;
        D3D12_GPU_DESCRIPTOR_HANDLE m_depthSource{};
        D3D12_GPU_DESCRIPTOR_HANDLE m_hierarchySource{};
        std::uint32_t m_capacity = 0;
        std::uint32_t m_depthWidth = 0;
        std::uint32_t m_depthHeight = 0;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
        std::uint32_t m_mipCount = 0;
        bool m_recorded = false;
        bool m_pending = false;
    };
} // namespace Engine
