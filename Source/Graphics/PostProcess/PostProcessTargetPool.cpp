#include "Pch.h"
#include "Graphics\PostProcess\PostProcessTargetPool.h"
#include "Graphics\DirectX12\Fence.h"

namespace Engine
{
    PostProcessTargetPool::~PostProcessTargetPool()
    {
        if (!finalize())
            LOG_CRITICAL("[PostProcess] Target pool shutdown failed.");
    }

    bool PostProcessTargetPool::initialize(ID3D12Device& device, const DX12Fence& fence,
        DX12DescriptorHeap& srvHeap)
    {
        if (!srvHeap.isShaderVisible() || srvHeap.get() == nullptr || fence.get() == nullptr)
        {
            LOG_ERROR("[PostProcess] Target pool requires a fence and shader-visible SRV heap.");
            return false;
        }
        if (!finalize())
            return false;
        m_device = &device;
        m_fence = &fence;
        m_srvHeap = &srvHeap;
        return true;
    }

    bool PostProcessTargetPool::releaseTargets()
    {
        for (const auto& target : m_targets)
            if (target->fenceValue != 0 && !m_fence->waitOnCpu(target->fenceValue))
                return false;
        for (const auto& target : m_targets)
        {
            target->resource.finalize();
            target->rtvHeap.finalize();
            if (target->srv)
            {
                if (!m_srvHeap->release(target->srv->cpu.index))
                {
                    LOG_ERROR("[PostProcess] Cannot release a temporary SRV.");
                    return false;
                }
                target->srv.reset();
            }
        }
        m_targets.clear();
        return true;
    }

    bool PostProcessTargetPool::finalize()
    {
        // Unsubmitted command lists are discarded by the renderer on shutdown.
        if (!releaseTargets())
            return false;
        m_recording = false;
        m_device = nullptr;
        m_fence = nullptr;
        m_srvHeap = nullptr;
        return true;
    }

    bool PostProcessTargetPool::clear()
    {
        if (m_recording)
        {
            LOG_ERROR("[PostProcess] Cannot clear targets before submission.");
            return false;
        }
        return releaseTargets();
    }

    bool PostProcessTargetPool::beginRecording()
    {
        if (m_device == nullptr || m_recording)
        {
            LOG_ERROR("[PostProcess] Target pool is uninitialized or has unsubmitted work.");
            return false;
        }
        m_recording = true;
        return true;
    }

    PostProcessImage* PostProcessTargetPool::acquire(const PostProcessTargetDescription& description)
    {
        if (!m_recording || description.width == 0 || description.height == 0
            || description.width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || description.height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || (description.format != DXGI_FORMAT_R16G16B16A16_FLOAT
                && description.format != DXGI_FORMAT_R32G32B32A32_FLOAT))
        {
            LOG_ERROR("[PostProcess] Invalid temporary target request.");
            return nullptr;
        }
        for (const auto& target : m_targets)
        {
            if (target->image.resource != nullptr && !target->borrowed && target->description == description
                && (target->touched || target->fenceValue == 0 || m_fence->isComplete(target->fenceValue)))
            {
                target->borrowed = true;
                target->touched = true;
                return &target->image;
            }
        }
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{ .Format = description.format };
        const auto required = D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
        const HRESULT supportResult = m_device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
        if (FAILED(supportResult) || (support.Support1 & required) != required)
        {
            LOG_ERROR("[PostProcess] Unsupported temporary texture format {}.",
                static_cast<unsigned int>(description.format));
            return nullptr;
        }
        // Own the entry before allocating descriptors so failure cleanup retains every allocation.
        auto target = std::make_unique<Target>();
        Target& entry = *target;
        m_targets.push_back(std::move(target));
        entry.description = description;
        D3D12_RESOURCE_DESC texture{};
        texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width = description.width;
        texture.Height = description.height;
        texture.DepthOrArraySize = 1;
        texture.MipLevels = 1;
        texture.Format = description.format;
        texture.SampleDesc = { 1, 0 };
        texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (!entry.resource.initialize(*m_device, { .description = texture })
            || !entry.rtvHeap.initialize(*m_device, {
                .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .capacity = 1 }))
            return nullptr;
        const auto rtv = entry.rtvHeap.allocate();
        entry.srv = m_srvHeap->allocate();
        if (!rtv || !entry.srv || !entry.srv->gpu)
        {
            LOG_ERROR("[PostProcess] Cannot allocate temporary texture views.");
            return nullptr;
        }
        m_device->CreateRenderTargetView(entry.resource.get(), nullptr, rtv->cpu.native);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = description.format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        m_device->CreateShaderResourceView(entry.resource.get(), &srv, entry.srv->cpu.native);
        entry.image = { &entry.resource, *entry.srv->gpu, rtv->cpu };
        entry.borrowed = true;
        entry.touched = true;
        return &entry.image;
    }

    bool PostProcessTargetPool::release(const PostProcessImage& image)
    {
        for (const auto& target : m_targets)
        {
            if (&target->image == &image && m_recording && target->borrowed)
            {
                target->borrowed = false;
                return true;
            }
        }
        LOG_ERROR("[PostProcess] Cannot return an unknown or unborrowed target.");
        return false;
    }

    bool PostProcessTargetPool::markSubmitted(const std::uint64_t fenceValue)
    {
        if (!m_recording || fenceValue == 0 || m_fence->getLastSignaledValue() < fenceValue)
        {
            LOG_ERROR("[PostProcess] Invalid target pool submission.");
            return false;
        }
        for (const auto& target : m_targets)
        {
            if (target->borrowed)
            {
                LOG_ERROR("[PostProcess] A temporary image was not returned before submission.");
                return false;
            }
        }
        for (const auto& target : m_targets)
        {
            if (target->touched)
            {
                target->fenceValue = fenceValue;
                target->touched = false;
            }
        }
        m_recording = false;
        return true;
    }
}
