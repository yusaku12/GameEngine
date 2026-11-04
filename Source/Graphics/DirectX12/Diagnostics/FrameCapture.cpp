#include "Pch.h"
#include "Graphics\DirectX12\Diagnostics\FrameCapture.h"
#include "Graphics\DirectX12\Rendering\Renderer.h"

namespace Engine
{
    bool DX12Renderer::captureFrame(DX12FrameCapture& destination)
    {
        destination.hdr.Release();
        destination.display.Release();
        if (m_device.get() == nullptr || m_renderFailed || m_lastSubmittedFenceValue == 0
            || (m_deferredFrameActive
                && (!m_deferredPassRecorded || m_deferredLightingTarget.get() == nullptr))
            || m_gameRenderTarget.get() == nullptr)
        {
            LOG_ERROR("[Capture] A successfully submitted frame is required.");
            return false;
        }
        if (!m_directFence.waitOnCpu(m_lastSubmittedFenceValue))
            return false;

        DX12FrameCapture captured;
        const auto capture = [this](DX12Resource& resource, DirectX::ScratchImage& image)
        {
            // CaptureTexture waits for its copy and restores the tracked state.
            const D3D12_RESOURCE_STATES state = resource.getCurrentState();
            const HRESULT result = DirectX::CaptureTexture(
                m_directQueue.get(), resource.get(), false, image, state, state);
            if (FAILED(result))
            {
                LOG_ERROR("[Capture] GPU readback failed (HRESULT: 0x{:08X}).",
                    static_cast<unsigned long>(result));
                return false;
            }
            return true;
        };
        if ((m_deferredFrameActive && !capture(m_deferredLightingTarget, captured.hdr))
            || !capture(m_gameRenderTarget, captured.display))
            return false;
        destination.hdr = std::move(captured.hdr);
        destination.display = std::move(captured.display);
        return true;
    }

    bool DX12FrameCapture::save(const std::filesystem::path& directory, const std::wstring_view name) const
    {
        const DirectX::Image* const hdrImage = hdr.GetImage(0, 0, 0);
        const DirectX::Image* const displayImage = display.GetImage(0, 0, 0);
        if (hdrImage == nullptr || displayImage == nullptr || name.empty()
            || name.find_first_of(L"\\/:*?\"<>|") != std::wstring_view::npos
            || name == L"." || name == L"..")
        {
            LOG_ERROR("[Capture] Images or output name are invalid.");
            return false;
        }
        const std::filesystem::path hdrPath = directory / (std::wstring(name) + L"-hdr.dds");
        const std::filesystem::path displayPath = directory / (std::wstring(name) + L"-display.png");
        std::error_code error;
        const auto canWrite = [&error](const std::filesystem::path& path)
        {
            const bool exists = std::filesystem::exists(path, error);
            if (error || exists)
            {
                LOG_ERROR("[Capture] Cannot write '{}': {}.", path.string(),
                    error ? error.message() : "file already exists");
                return false;
            }
            return true;
        };
        if (!canWrite(hdrPath) || !canWrite(displayPath))
            return false;
        HRESULT result = DirectX::SaveToDDSFile(*hdrImage, DirectX::DDS_FLAGS_NONE, hdrPath.c_str());
        if (FAILED(result))
        {
            LOG_ERROR("[Capture] HDR DDS save failed (HRESULT: 0x{:08X}).",
                static_cast<unsigned long>(result));
            return false;
        }
        result = DirectX::SaveToWICFile(*displayImage, DirectX::WIC_FLAGS_FORCE_SRGB,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), displayPath.c_str());
        if (FAILED(result))
        {
            LOG_ERROR("[Capture] Display PNG save failed (HRESULT: 0x{:08X}).",
                static_cast<unsigned long>(result));
            return false;
        }
        return true;
    }
}
