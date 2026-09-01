#include "Pch.h"
#include "Graphics\DirectX12\Pipeline.h"

#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Shader.h"

namespace Engine
{
	bool DX12GraphicsPipeline::initialize(ID3D12Device& device, const DX12GraphicsPipelineConfig& config)
	{
		if (m_pipelineState != nullptr || m_rootSignature != nullptr)
		{
			LOG_ERROR("[DX12] 初期化済みの Graphics Pipeline を再初期化しようとしました");
			return false;
		}

		if (config.vertexShader == nullptr || config.pixelShader == nullptr
			|| !config.vertexShader->isCompiled() || !config.pixelShader->isCompiled())
		{
			LOG_ERROR("[DX12] Graphics Pipeline にコンパイル済みの Vertex/Pixel Shader が必要です");
			return false;
		}

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
		rootSignatureDescription.NumParameters = 0;
		rootSignatureDescription.pParameters = nullptr;
		rootSignatureDescription.NumStaticSamplers = 0;
		rootSignatureDescription.pStaticSamplers = nullptr;
		rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature;
		Microsoft::WRL::ComPtr<ID3DBlob> rootSignatureErrors;
		HRESULT result = D3D12SerializeRootSignature(
			&rootSignatureDescription,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&serializedRootSignature,
			&rootSignatureErrors);
		if (FAILED(result))
		{
			if (rootSignatureErrors != nullptr)
			{
				const std::string errorMessage(
					static_cast<const char*>(rootSignatureErrors->GetBufferPointer()),
					rootSignatureErrors->GetBufferSize());
				LOG_ERROR("[DX12] Root Signature のシリアライズに失敗しました: {}", errorMessage);
			}
			else
			{
				LOG_ERROR("[DX12] Root Signature のシリアライズに失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
			}
			return false;
		}

		result = device.CreateRootSignature(
			0,
			serializedRootSignature->GetBufferPointer(),
			serializedRootSignature->GetBufferSize(),
			IID_PPV_ARGS(&m_rootSignature));
		if (FAILED(result))
		{
			LOG_ERROR("[DX12] Root Signature の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
			finalize();
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
		description.pRootSignature = m_rootSignature.Get();
		description.VS = config.vertexShader->getBytecode();
		description.PS = config.pixelShader->getBytecode();
		description.BlendState.AlphaToCoverageEnable = FALSE;
		description.BlendState.IndependentBlendEnable = FALSE;
		const D3D12_RENDER_TARGET_BLEND_DESC renderTargetBlend{
			.BlendEnable = FALSE,
			.LogicOpEnable = FALSE,
			.SrcBlend = D3D12_BLEND_ONE,
			.DestBlend = D3D12_BLEND_ZERO,
			.BlendOp = D3D12_BLEND_OP_ADD,
			.SrcBlendAlpha = D3D12_BLEND_ONE,
			.DestBlendAlpha = D3D12_BLEND_ZERO,
			.BlendOpAlpha = D3D12_BLEND_OP_ADD,
			.LogicOp = D3D12_LOGIC_OP_NOOP,
			.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
		};
		for (D3D12_RENDER_TARGET_BLEND_DESC& target : description.BlendState.RenderTarget)
			target = renderTargetBlend;

		description.SampleMask = UINT_MAX;
		description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		description.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
		description.RasterizerState.FrontCounterClockwise = FALSE;
		description.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		description.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		description.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		description.RasterizerState.DepthClipEnable = TRUE;
		description.RasterizerState.MultisampleEnable = FALSE;
		description.RasterizerState.AntialiasedLineEnable = FALSE;
		description.RasterizerState.ForcedSampleCount = 0;
		description.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		description.DepthStencilState.DepthEnable = config.depthStencilFormat != DXGI_FORMAT_UNKNOWN;
		description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		description.DepthStencilState.StencilEnable = FALSE;
		description.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		description.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		description.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		description.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		description.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		description.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		description.DepthStencilState.BackFace = description.DepthStencilState.FrontFace;
		description.InputLayout = { config.inputLayout.data(), static_cast<UINT>(config.inputLayout.size()) };
		description.PrimitiveTopologyType = config.primitiveTopology;
		description.NumRenderTargets = 1;
		description.RTVFormats[0] = config.renderTargetFormat;
		description.DSVFormat = config.depthStencilFormat;
		description.SampleDesc = { 1, 0 };

		result = device.CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&m_pipelineState));
		if (FAILED(result))
		{
			LOG_ERROR("[DX12] Graphics PSO の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
			finalize();
			return false;
		}

		return true;
	}

	void DX12GraphicsPipeline::finalize()
	{
		m_pipelineState.Reset();
		m_rootSignature.Reset();
	}

	bool DX12GraphicsPipeline::bind(DX12CommandList& commandList) const
	{
		ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
		if (nativeCommandList == nullptr || m_rootSignature == nullptr || m_pipelineState == nullptr)
		{
			LOG_ERROR("[DX12] 初期化済み Pipeline を記録中 Command List へ bind する必要があります");
			return false;
		}

		nativeCommandList->SetGraphicsRootSignature(m_rootSignature.Get());
		nativeCommandList->SetPipelineState(m_pipelineState.Get());
		return true;
	}
} // namespace Engine