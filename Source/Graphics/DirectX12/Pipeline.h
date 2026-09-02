#pragma once

#include "Core\CoreDefines.h"

namespace Engine
{
    class DX12CommandList;
    class DX12Shader;

    /**
     * @brief Graphics Pipeline State の作成設定
     */
    struct DX12GraphicsPipelineConfig
    {
        const DX12Shader* vertexShader = nullptr; //!< Vertex Shader
        const DX12Shader* pixelShader = nullptr; //!< Pixel Shader
        std::span<const D3D12_INPUT_ELEMENT_DESC> inputLayout; //!< Vertex Input Layout
        DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM; //!< Render Target Format
        DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN; //!< Depth Stencil Format
        D3D12_PRIMITIVE_TOPOLOGY_TYPE primitiveTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; //!< Primitive Topology 種別
    };

    /**
     * @brief Root Signature と Graphics Pipeline State をキャッシュするクラス
     * @details 初期化後は PSO を再作成せず、記録中の Command List へ bind する。
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class DX12GraphicsPipeline
    {
    public:

        DX12GraphicsPipeline() = default;
        ~DX12GraphicsPipeline() = default;

        GE_DISABLE_COPY_AND_MOVE(DX12GraphicsPipeline);

        /**
         * @brief 空の Root Signature と Graphics PSO を作成する
         * @param device PSO を作成するデバイス
         * @param config Shader、Input Layout、Render Target Format の設定
         * @return 作成に成功した場合は true
         */
        bool initialize(ID3D12Device& device, const DX12GraphicsPipelineConfig& config);

        /**
         * @brief Root Signature と PSO を解放する
         * @warning GPU が Pipeline State を使用していないことを Fence で確認してから呼び出すこと
         */
        void finalize();

        /**
         * @brief 記録中の Command List に Root Signature と PSO を設定する
         * @param commandList 設定先の Command List
         * @return 設定に成功した場合は true
         */
        bool bind(DX12CommandList& commandList) const;

        /**
         * @brief Graphics PSO を取得する
         * @return 非所有の PSO 参照。未初期化時は nullptr
         */
        ID3D12PipelineState* get() const noexcept { return m_pipelineState.Get(); }

    private:

        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature; //!< Root Signature
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState; //!< キャッシュ済み Graphics PSO
    };
} // namespace Engine
