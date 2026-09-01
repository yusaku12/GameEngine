#include "Pch.h"
#include "Graphics\DirectX12\Descriptor.h"

namespace Engine
{
	bool DX12DescriptorHeap::initialize(ID3D12Device& device, const DX12DescriptorHeapConfig& config)
	{
		finalize();

		if (config.capacity == 0)
		{
			LOG_ERROR("[DX12] Descriptor Heap の容量は 1 以上である必要があります");
			return false;
		}

		const bool shaderVisibleType = config.type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
			|| config.type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		if (config.shaderVisible && !shaderVisibleType)
		{
			LOG_ERROR("[DX12] RTV/DSV Descriptor Heap を Shader Visible にすることはできません");
			return false;
		}

		D3D12_DESCRIPTOR_HEAP_DESC description{};
		description.Type = config.type;
		description.NumDescriptors = config.capacity;
		description.Flags = config.shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		description.NodeMask = 0;

		const HRESULT result = device.CreateDescriptorHeap(&description, IID_PPV_ARGS(&m_heap));
		if (FAILED(result))
		{
			LOG_ERROR("[DX12] Descriptor Heap の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
			return false;
		}

		m_cpuStart = m_heap->GetCPUDescriptorHandleForHeapStart();
		if (config.shaderVisible)
			m_gpuStart = m_heap->GetGPUDescriptorHandleForHeapStart();

		m_descriptorSize = device.GetDescriptorHandleIncrementSize(config.type);
		m_capacity = config.capacity;
		m_shaderVisible = config.shaderVisible;
		return true;
	}

	void DX12DescriptorHeap::finalize()
	{
		m_heap.Reset();
		m_cpuStart = {};
		m_gpuStart = {};
		m_descriptorSize = 0;
		m_capacity = 0;
		m_nextIndex = 0;
		m_shaderVisible = false;
	}

	std::optional<DX12DescriptorAllocation> DX12DescriptorHeap::allocate()
	{
		if (m_heap == nullptr)
		{
			LOG_ERROR("[DX12] 未初期化の Descriptor Heap から割り当てようとしました");
			return std::nullopt;
		}

		if (m_nextIndex == m_capacity)
		{
			LOG_ERROR("[DX12] Descriptor Heap の容量が不足しています (容量: {})", m_capacity);
			return std::nullopt;
		}

		const SIZE_T offset = static_cast<SIZE_T>(m_nextIndex) * m_descriptorSize;
		DX12DescriptorAllocation allocation{};
		allocation.cpu.native.ptr = m_cpuStart.ptr + offset;
		allocation.cpu.index = m_nextIndex;
		if (m_shaderVisible)
		{
			allocation.gpu = DX12GpuDescriptorHandle{
				.native = { m_gpuStart.ptr + offset },
				.index = m_nextIndex,
			};
		}

		++m_nextIndex;
		return allocation;
	}
} // namespace Engine