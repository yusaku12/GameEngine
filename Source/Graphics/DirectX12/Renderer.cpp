#include "Pch.h"
#include "Graphics\DirectX12\Renderer.h"

namespace Engine
{
	namespace
	{
		struct TriangleVertex
		{
			float position[3];
			float color[3];
		};

		constexpr std::array TRIANGLE_VERTICES = {
			TriangleVertex{ { 0.0f, 0.6f, 0.0f }, { 0.95f, 0.25f, 0.20f } },
			TriangleVertex{ { 0.6f, -0.6f, 0.0f }, { 0.20f, 0.80f, 0.35f } },
			TriangleVertex{ { -0.6f, -0.6f, 0.0f }, { 0.20f, 0.50f, 0.95f } },
		};

		constexpr std::array INPUT_LAYOUT = {
			D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
	}

	bool DX12Renderer::initialize(const HWND hwnd, const std::uint32_t width, const std::uint32_t height)
	{
		if (hwnd == nullptr || width == 0 || height == 0)
			return false;

		if (!finalize())
			return false;

		DX12DeviceConfig deviceConfig{};
#if defined(_DEBUG)
		deviceConfig.enableDebugLayer = true;
		deviceConfig.enableGpuBasedValidation = true;
		deviceConfig.enableDxgiDebug = true;
#endif
		if (!m_device.initialize(deviceConfig))
			return false;

		if (!m_directQueue.initialize(*m_device.get(), DX12CommandQueueType::DIRECT)
			|| !m_directFence.initialize(*m_device.get()))
		{
			finalize();
			return false;
		}

		DX12SwapChainConfig swapChainConfig{};
		swapChainConfig.bufferCount = FRAME_COUNT;
		if (!m_swapChain.initialize(
			*m_device.get(),
			*m_device.getFactory(),
			*m_directQueue.get(),
			hwnd,
			width,
			height,
			swapChainConfig))
		{
			finalize();
			return false;
		}

		for (DX12CommandList& commandList : m_commandLists)
		{
			if (!commandList.initialize(*m_device.get(), DX12CommandQueueType::DIRECT))
			{
				finalize();
				return false;
			}
		}

		const DX12ShaderConfig vertexShaderConfig{
			.sourcePath = "Assets/Shaders/ColorTriangle.hlsl",
			.entryPoint = "vsMain",
			.targetProfile = "vs_5_1",
		};
		const DX12ShaderConfig pixelShaderConfig{
			.sourcePath = "Assets/Shaders/ColorTriangle.hlsl",
			.entryPoint = "psMain",
			.targetProfile = "ps_5_1",
		};
		if (!m_vertexShader.compile(vertexShaderConfig) || !m_pixelShader.compile(pixelShaderConfig))
		{
			finalize();
			return false;
		}

		const DX12GraphicsPipelineConfig pipelineConfig{
			.vertexShader = &m_vertexShader,
			.pixelShader = &m_pixelShader,
			.inputLayout = INPUT_LAYOUT,
			.renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
		};
		if (!m_graphicsPipeline.initialize(*m_device.get(), pipelineConfig)
			|| !m_vertexBuffer.initialize(*m_device.get(), m_directFence, sizeof(TRIANGLE_VERTICES)))
		{
			finalize();
			return false;
		}

		const std::span<const TriangleVertex> vertices = TRIANGLE_VERTICES;
		if (!m_vertexBuffer.write(std::as_bytes(vertices)))
		{
			finalize();
			return false;
		}

		m_vertexBufferView.BufferLocation = m_vertexBuffer.getGpuVirtualAddress();
		m_vertexBufferView.SizeInBytes = sizeof(TRIANGLE_VERTICES);
		m_vertexBufferView.StrideInBytes = sizeof(TriangleVertex);

		m_frameFenceValues.fill(0);
		m_lastSubmittedFenceValue = 0;
		m_renderWidth = width;
		m_renderHeight = height;
		return true;
	}

	bool DX12Renderer::finalize()
	{
		if (!m_swapChain.finalize(m_directFence, m_lastSubmittedFenceValue))
			return false;

		if (!m_vertexBuffer.finalize())
			return false;

		m_graphicsPipeline.finalize();
		m_pixelShader.finalize();
		m_vertexShader.finalize();
		for (DX12CommandList& commandList : m_commandLists)
			commandList.finalize();

		m_frameFenceValues.fill(0);
		m_lastSubmittedFenceValue = 0;
		m_vertexBufferView = {};
		m_renderWidth = 0;
		m_renderHeight = 0;
		m_directFence.finalize();
		m_directQueue.finalize();
		m_device.finalize();
		return true;
	}

	bool DX12Renderer::render()
	{
		const std::uint32_t frameIndex = m_swapChain.getCurrentBackBufferIndex();
		if (frameIndex >= FRAME_COUNT)
		{
			LOG_ERROR("[DX12] 不正な Back Buffer Index です: {}", frameIndex);
			return false;
		}

		const std::uint64_t frameFenceValue = m_frameFenceValues[frameIndex];
		if (frameFenceValue != 0 && !m_directFence.isComplete(frameFenceValue)
			&& !m_directFence.waitOnCpu(frameFenceValue))
		{
			return false;
		}

		DX12CommandList& commandList = m_commandLists[frameIndex];
		DX12Resource* const backBuffer = m_swapChain.getCurrentBackBuffer();
		if (backBuffer == nullptr || !commandList.begin(m_directFence)
			|| !backBuffer->transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET))
		{
			return false;
		}

		static constexpr float CLEAR_COLOR[] = { 0.08f, 0.16f, 0.24f, 1.0f };
		ID3D12GraphicsCommandList* const nativeCommandList = commandList.getForRecording();
		const D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_swapChain.getCurrentRtv().native;
		const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight), 0.0f, 1.0f };
		const D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>(m_renderWidth), static_cast<LONG>(m_renderHeight) };
		nativeCommandList->OMSetRenderTargets(1, &renderTargetView, FALSE, nullptr);
		nativeCommandList->RSSetViewports(1, &viewport);
		nativeCommandList->RSSetScissorRects(1, &scissorRect);
		nativeCommandList->ClearRenderTargetView(renderTargetView, CLEAR_COLOR, 0, nullptr);
		if (!m_graphicsPipeline.bind(commandList))
			return false;

		nativeCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		nativeCommandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		nativeCommandList->DrawInstanced(static_cast<UINT>(TRIANGLE_VERTICES.size()), 1, 0, 0);

		if (!backBuffer->transition(commandList, D3D12_RESOURCE_STATE_PRESENT) || !commandList.close())
			return false;

		ID3D12CommandList* const nativeExecutionList = commandList.getForExecution();
		if (nativeExecutionList == nullptr)
			return false;

		const std::array<ID3D12CommandList*, 1> commandLists = { nativeExecutionList };
		m_directQueue.execute(commandLists);
		const bool presentSucceeded = m_swapChain.present(true);
		if (!presentSucceeded)
			m_device.logDeviceRemovedReason();

		const std::uint64_t submittedFenceValue = m_directFence.signal(*m_directQueue.get());
		if (submittedFenceValue == 0)
		{
			m_device.logDeviceRemovedReason();
			return false;
		}

		if (!commandList.markSubmitted(submittedFenceValue))
			return false;
		if (!m_vertexBuffer.markUsed(submittedFenceValue))
			return false;

		m_frameFenceValues[frameIndex] = submittedFenceValue;
		m_lastSubmittedFenceValue = submittedFenceValue;
		return presentSucceeded;
	}

	bool DX12Renderer::resize(const std::uint32_t width, const std::uint32_t height)
	{
		if (width == 0 || height == 0)
			return true;

		if (!m_swapChain.resize(*m_device.get(), m_directFence, m_lastSubmittedFenceValue, width, height))
			return false;

		m_frameFenceValues.fill(0);
		m_lastSubmittedFenceValue = 0;
		m_renderWidth = width;
		m_renderHeight = height;
		return true;
	}
} // namespace Engine