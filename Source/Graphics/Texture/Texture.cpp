#include "Pch.h"
#include "Graphics\Texture\Texture.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\DirectX12\Queue.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    bool Texture::initialize(
        ID3D12Device& device,
        DX12DescriptorHeap& descriptorHeap,
        DX12CommandQueue& directQueue,
        DX12Fence& fence,
        const std::filesystem::path& path,
        const TextureLoadDesc& desc)
    {
        finalize();

        if (path.empty() || !std::filesystem::exists(path))
        {
            LOG_ERROR("[Texture] ファイルが見つかりません: {}", path.string());
            return false;
        }

        m_path = std::filesystem::weakly_canonical(path);
        m_info.colorSpace = desc.colorSpace;
        m_state = TextureState::Loading;

        DirectX::ScratchImage scratchImage = loadImageFromFile(m_path);
        if (scratchImage.GetImageCount() == 0)
        {
            LOG_ERROR("[Texture] 画像ロード失敗: {}", m_path.string());
            m_state = TextureState::Failed;
            return false;
        }

        const auto& metadata = scratchImage.GetMetadata();
        if (m_info.colorSpace == TextureColorSpace::Auto)
            m_info.colorSpace = DirectX::IsSRGB(metadata.format) ? TextureColorSpace::SRGB : TextureColorSpace::Linear;

        if (desc.generateMips && metadata.mipLevels == 1)
        {
            DirectX::ScratchImage mipImage;
            const HRESULT mipResult = DirectX::GenerateMipMaps(
                scratchImage.GetImages(), scratchImage.GetImageCount(), metadata,
                DirectX::TEX_FILTER_DEFAULT, 0, mipImage);
            if (SUCCEEDED(mipResult))
                scratchImage = std::move(mipImage);
            else
                LOG_WARNING("[Texture] Mipmap生成に失敗しました (HRESULT: 0x{:08X})", static_cast<unsigned int>(mipResult));
        }

        m_state = TextureState::Uploading;
        if (!createGpuResource(device, descriptorHeap, directQueue, fence, scratchImage))
        {
            LOG_ERROR("[Texture] GPUリソース作成またはアップロードに失敗しました: {}", m_path.string());
            m_state = TextureState::Failed;
            return false;
        }

        m_state = TextureState::Ready;
        LOG_INFO("[Texture] テクスチャロード: {} ({}x{}, Mips: {}, Format: {})",
            m_path.filename().string(), m_info.width, m_info.height, m_info.mipLevels,
            static_cast<int>(m_info.format));
        return true;
    }

    bool Texture::initializeSolidColor(
        ID3D12Device& device,
        DX12DescriptorHeap& descriptorHeap,
        DX12CommandQueue& directQueue,
        DX12Fence& fence,
        const std::array<std::uint8_t, 4>& color,
        TextureColorSpace colorSpace)
    {
        finalize();
        DirectX::ScratchImage image;
        if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, 1)) || image.GetPixels() == nullptr)
            return false;
        std::memcpy(image.GetPixels(), color.data(), color.size());
        m_info.colorSpace = colorSpace;
        m_state = TextureState::Uploading;
        if (!createGpuResource(device, descriptorHeap, directQueue, fence, image))
        {
            m_state = TextureState::Failed;
            return false;
        }
        m_state = TextureState::Ready;
        return true;
    }

    void Texture::finalize()
    {
        if (m_uploadFence != nullptr && m_uploadFenceValue != 0)
        {
            if (!m_uploadFence->isComplete(m_uploadFenceValue))
                m_uploadFence->waitOnCpu(m_uploadFenceValue);
        }

        m_uploadBuffer.Reset();
        m_gpuResource.Reset();
        m_path.clear();
        m_info = TextureResourceInfo();
        m_state = TextureState::Unloaded;
        m_uploadFence = nullptr;
        m_uploadFenceValue = 0;
    }

    const TextureResourceInfo* Texture::getResourceInfo() const noexcept
    {
        return m_state == TextureState::Ready ? &m_info : nullptr;
    }

    uint64_t Texture::getGPUMemorySize() const noexcept
    {
        if (m_gpuResource == nullptr)
            return 0;

        const std::uint64_t bitsPerPixel = DirectX::BitsPerPixel(m_info.format);
        if (bitsPerPixel == 0)
            return 0;
        std::uint64_t totalSize = 0;
        std::uint64_t width = m_info.width;
        std::uint64_t height = m_info.height;
        for (std::uint32_t mip = 0; mip < m_info.mipLevels; ++mip)
        {
            totalSize += std::max<std::uint64_t>(1, width) * std::max<std::uint64_t>(1, height) * bitsPerPixel / 8;
            width = std::max<std::uint64_t>(1, width / 2);
            height = std::max<std::uint64_t>(1, height / 2);
        }
        return totalSize;
    }

    DirectX::ScratchImage Texture::loadImageFromFile(const std::filesystem::path& path)
    {
        DirectX::ScratchImage scratchImage;
        const std::wstring wPath = path.wstring();
        const auto ext = path.extension().string();
        std::string extLower = ext;
        std::transform(extLower.begin(), extLower.end(), extLower.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

        HRESULT hr = E_FAIL;
        if (extLower == ".dds")
            hr = DirectX::LoadFromDDSFile(wPath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, scratchImage);
        else if (extLower == ".tga")
            hr = DirectX::LoadFromTGAFile(wPath.c_str(), nullptr, scratchImage);
        else if (extLower == ".hdr")
            hr = DirectX::LoadFromHDRFile(wPath.c_str(), nullptr, scratchImage);
        else if (extLower != ".png" && extLower != ".jpg" && extLower != ".jpeg"
            && extLower != ".bmp" && extLower != ".tif" && extLower != ".tiff" && extLower != ".gif")
        {
            LOG_ERROR("[Texture] 未対応の拡張子です: {}", path.extension().string());
            return scratchImage;
        }
        else
            hr = DirectX::LoadFromWICFile(wPath.c_str(), DirectX::WIC_FLAGS_NONE, nullptr, scratchImage);

        if (FAILED(hr))
            LOG_ERROR("[Texture] ファイル読み込み失敗: {} (0x{:08X})", path.string(), static_cast<unsigned int>(hr));

        return scratchImage;
    }

    DXGI_FORMAT Texture::determineFormat(const DirectX::TexMetadata& metadata, TextureColorSpace colorSpace)
    {
        DXGI_FORMAT format = metadata.format;
        if (colorSpace == TextureColorSpace::SRGB)
        {
            if (format == DXGI_FORMAT_R8G8B8A8_UNORM) format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            else if (format == DXGI_FORMAT_BC1_UNORM) format = DXGI_FORMAT_BC1_UNORM_SRGB;
            else if (format == DXGI_FORMAT_BC2_UNORM) format = DXGI_FORMAT_BC2_UNORM_SRGB;
            else if (format == DXGI_FORMAT_BC3_UNORM) format = DXGI_FORMAT_BC3_UNORM_SRGB;
            else if (format == DXGI_FORMAT_B8G8R8A8_UNORM) format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        }
        else if (colorSpace == TextureColorSpace::Linear)
        {
            if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) format = DXGI_FORMAT_R8G8B8A8_UNORM;
            else if (format == DXGI_FORMAT_BC1_UNORM_SRGB) format = DXGI_FORMAT_BC1_UNORM;
            else if (format == DXGI_FORMAT_BC2_UNORM_SRGB) format = DXGI_FORMAT_BC2_UNORM;
            else if (format == DXGI_FORMAT_BC3_UNORM_SRGB) format = DXGI_FORMAT_BC3_UNORM;
            else if (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) format = DXGI_FORMAT_B8G8R8A8_UNORM;
        }
        return format;
    }

    DirectX::ScratchImage Texture::generateMipmaps(const DirectX::ScratchImage& scratchImage, bool generateMips)
    {
        if (!generateMips || scratchImage.GetMetadata().mipLevels > 1)
            return {};
        DirectX::ScratchImage result;
        if (FAILED(DirectX::GenerateMipMaps(scratchImage.GetImages(), scratchImage.GetImageCount(),
            scratchImage.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, result)))
            return {};
        return result;
    }

    bool Texture::createGpuResource(
        ID3D12Device& device,
        DX12DescriptorHeap& descriptorHeap,
        DX12CommandQueue& directQueue,
        DX12Fence& fence,
        const DirectX::ScratchImage& scratchImage)
    {
        const DirectX::TexMetadata metadata = scratchImage.GetMetadata();
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        HRESULT result = DirectX::CreateTexture(&device, metadata, resource.GetAddressOf());
        if (FAILED(result))
            return false;

        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        result = DirectX::PrepareUpload(&device, scratchImage.GetImages(), scratchImage.GetImageCount(), metadata, subresources);
        if (FAILED(result) || subresources.empty())
            return false;

        const UINT64 uploadSize = GetRequiredIntermediateSize(resource.Get(), 0, static_cast<UINT>(subresources.size()));
        const auto uploadProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const auto uploadDescription = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        result = device.CreateCommittedResource(&uploadProperties, D3D12_HEAP_FLAG_NONE, &uploadDescription,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_uploadBuffer.ReleaseAndGetAddressOf()));
        if (FAILED(result))
            return false;

        DX12CommandList commandList;
        if (!commandList.initialize(device, DX12CommandQueueType::DIRECT) || !commandList.begin(fence))
            return false;
        ID3D12GraphicsCommandList* nativeList = commandList.getForRecording();
        if (nativeList == nullptr)
            return false;

        const auto toCopyDestination = CD3DX12_RESOURCE_BARRIER::Transition(
            resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        nativeList->ResourceBarrier(1, &toCopyDestination);
        if (UpdateSubresources(nativeList, resource.Get(), m_uploadBuffer.Get(), 0, 0,
            static_cast<UINT>(subresources.size()), subresources.data()) == 0)
            return false;
        const auto toShaderResource = CD3DX12_RESOURCE_BARRIER::Transition(
            resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        nativeList->ResourceBarrier(1, &toShaderResource);
        if (!commandList.close())
            return false;

        ID3D12CommandList* executionList = commandList.getForExecution();
        directQueue.execute(std::span<ID3D12CommandList* const>(&executionList, 1));
        const std::uint64_t fenceValue = fence.signal(*directQueue.get());
        if (fenceValue == 0)
            return false;
        commandList.markSubmitted(fenceValue);

        m_gpuResource = resource;
        m_uploadFence = &fence;
        m_uploadFenceValue = fenceValue;
        m_resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_info.resource = m_gpuResource.Get();
        m_info.width = static_cast<std::uint32_t>(metadata.width);
        m_info.height = static_cast<std::uint32_t>(metadata.height);
        m_info.mipLevels = static_cast<std::uint32_t>(metadata.mipLevels);
        m_info.format = determineFormat(metadata, m_info.colorSpace);
        m_info.type = metadata.IsCubemap() ? TextureType::TextureCube : TextureType::Texture2D;
        return createShaderResourceView(device, descriptorHeap);
    }

    bool Texture::createShaderResourceView(ID3D12Device& device, DX12DescriptorHeap& descriptorHeap)
    {
        const auto allocation = descriptorHeap.allocate();
        if (!allocation.has_value() || !allocation->gpu.has_value())
            return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC description{};
        description.Format = m_info.format;
        description.ViewDimension = m_info.type == TextureType::TextureCube
            ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
        description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (description.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
        {
            description.TextureCube.MipLevels = m_info.mipLevels;
            description.TextureCube.MostDetailedMip = 0;
        }
        else
        {
            description.Texture2D.MipLevels = m_info.mipLevels;
            description.Texture2D.MostDetailedMip = 0;
        }
        device.CreateShaderResourceView(m_gpuResource.Get(), &description, allocation->cpu.native);
        m_info.srv = allocation->gpu->native;
        m_info.srvIndex = allocation->gpu->index;
        return true;
    }
} // namespace Engine