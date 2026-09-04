#include "Pch.h"
#include "Graphics\DirectX12\Device.h"

namespace Engine
{
    bool DX12Device::initialize(const DX12DeviceConfig& config)
    {
        finalize();

        UINT factoryFlags = 0;
        if (config.enableDebugLayer)
        {
            if (!enableDebugLayer(config.enableGpuBasedValidation))
                return false;

            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }

        if (config.enableDxgiDebug)
            enableDxgiDebug();

        HRESULT result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] DXGI Factory の作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            finalize();
            return false;
        }

        if (!selectAdapter() || !createDevice() || !queryCapabilities())
        {
            finalize();
            return false;
        }

        LOG_INFO("[DX12] DirectX 12 デバイスを初期化しました");
        return true;
    }

    void DX12Device::finalize()
    {
        m_device.Reset();
        m_adapter.Reset();
        m_factory.Reset();
        if (m_dxgiDebug != nullptr)
        {
            m_dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
            m_dxgiDebug.Reset();
        }

        m_capabilities = {};
    }

    bool DX12Device::enableDebugLayer(const bool enableGpuBasedValidation)
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        const HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] D3D12 デバッグレイヤーの取得に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        debugController->EnableDebugLayer();

        if (enableGpuBasedValidation)
        {
            Microsoft::WRL::ComPtr<ID3D12Debug1> debugController1;
            const HRESULT debug1Result = debugController.As(&debugController1);
            if (SUCCEEDED(debug1Result))
            {
                debugController1->SetEnableGPUBasedValidation(TRUE);
            }
            else
            {
                LOG_WARNING("[DX12] GPU-based validation を利用できません (HRESULT: 0x{:08X})", static_cast<unsigned long>(debug1Result));
            }
        }

        return true;
    }

    void DX12Device::enableDxgiDebug()
    {
        const HRESULT result = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiDebug));
        if (FAILED(result))
        {
            LOG_WARNING("[DX12] DXGI Debug Interface を利用できません (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            m_dxgiDebug.Reset();
        }
    }

    bool DX12Device::selectAdapter()
    {
        for (UINT index = 0;; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
            const HRESULT result = m_factory->EnumAdapterByGpuPreference(
                index,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&candidate));
            if (result == DXGI_ERROR_NOT_FOUND)
                break;

            if (FAILED(result))
            {
                LOG_ERROR("[DX12] GPU アダプターの列挙に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
                return false;
            }

            DXGI_ADAPTER_DESC1 description{};
            const HRESULT descriptionResult = candidate->GetDesc1(&description);
            if (FAILED(descriptionResult))
            {
                LOG_ERROR("[DX12] GPU アダプター情報の取得に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(descriptionResult));
                return false;
            }

            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                continue;

            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
            {
                const HRESULT castResult = candidate.As(&m_adapter);
                if (SUCCEEDED(castResult))
                    return true;

                LOG_ERROR("[DX12] GPU アダプターの変換に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(castResult));
                return false;
            }
        }

        LOG_ERROR("[DX12] DirectX 12 Feature Level 12_0 対応のハードウェアアダプターが見つかりません");
        return false;
    }

    bool DX12Device::createDevice()
    {
        const HRESULT result = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] DirectX 12 デバイスの作成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        return true;
    }

    bool DX12Device::queryCapabilities()
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        const HRESULT result = m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
        if (FAILED(result))
        {
            LOG_ERROR("[DX12] DirectX 12 機能の取得に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned long>(result));
            return false;
        }

        m_capabilities.resourceBindingTier = options.ResourceBindingTier;
        struct ShaderModelFeature
        {
            UINT highestShaderModel;
        } shaderModel{ 0x67 };
        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))))
        {
            shaderModel.highestShaderModel = 0x60;
            LOG_WARNING("[DX12] Shader Model 6.7 query failed; using 6.0");
        }
        m_capabilities.shaderModel = shaderModel.highestShaderModel;
        LOG_INFO("[DX12] Highest supported Shader Model: 0x{:X}", m_capabilities.shaderModel);
        return true;
    }

    bool DX12Device::logDeviceRemovedReason() const
    {
        if (m_device == nullptr)
            return false;

        const HRESULT result = m_device->GetDeviceRemovedReason();
        if (SUCCEEDED(result))
            return false;

        LOG_CRITICAL("[DX12] Device Removed を検出しました (Reason: 0x{:08X})", static_cast<unsigned long>(result));
        return true;
    }
} // namespace Engine