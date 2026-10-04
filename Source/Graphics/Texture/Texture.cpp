#include "Pch.h"
#include "Graphics\Texture\Texture.h"
#include "Graphics\Texture\TextureImageUtils.h"
#include "Graphics\DirectX12\Command.h"
#include "Graphics\DirectX12\Fence.h"
#include "Graphics\DirectX12\Queue.h"
#include "Core\Logging\Logger.h"

namespace Engine
{
    Texture::Texture() = default;

    Texture::~Texture()
    {
        finalize();
    }

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

        const HRESULT preparationResult = TextureImageUtils::prepare(scratchImage, desc);
        if (FAILED(preparationResult))
        {
            LOG_ERROR("[Texture] 画像の準備に失敗しました: {} (0x{:08X})",
                m_path.string(), static_cast<unsigned int>(preparationResult));
            m_state = TextureState::Failed;
            return false;
        }
        if (preparationResult == S_FALSE)
            LOG_WARNING("[Texture] BC画像のミップは事前変換で生成してください: {}", m_path.string());
        m_info.colorSpace = DirectX::IsSRGB(scratchImage.GetMetadata().format)
            ? TextureColorSpace::SRGB : TextureColorSpace::Linear;

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
        if (!image.OverrideFormat(colorSpace == TextureColorSpace::SRGB
            ? DirectX::MakeSRGB(image.GetMetadata().format) : image.GetMetadata().format))
        {
            LOG_ERROR("[Texture] 単色画像の色空間設定に失敗しました");
            m_state = TextureState::Failed;
            return false;
        }
        m_info.colorSpace = DirectX::IsSRGB(image.GetMetadata().format)
            ? TextureColorSpace::SRGB : TextureColorSpace::Linear;
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

        m_uploadCommandList.reset();
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

        const auto resourceDesc = m_gpuResource->GetDesc();
        DirectX::TexMetadata metadata{};
        metadata.width = m_info.width;
        metadata.height = m_info.height;
        metadata.arraySize = resourceDesc.DepthOrArraySize;
        metadata.mipLevels = m_info.mipLevels;
        metadata.format = m_info.format;
        metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
        std::uint64_t totalSize = 0;
        const HRESULT result = TextureImageUtils::dataSize(metadata, totalSize);
        if (FAILED(result))
        {
            LOG_ERROR("[Texture] サイズ計算に失敗しました (0x{:08X})", static_cast<unsigned int>(result));
            return 0;
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

        auto uploadCommandList = std::make_unique<DX12CommandList>();
        auto& commandList = *uploadCommandList;
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

        m_uploadCommandList = std::move(uploadCommandList);
        m_gpuResource = resource;
        ID3D12CommandList* executionList = commandList.getForExecution();
        directQueue.execute(std::span<ID3D12CommandList* const>(&executionList, 1));
        const std::uint64_t fenceValue = fence.signal(*directQueue.get());
        if (fenceValue == 0)
            return false;
        commandList.markSubmitted(fenceValue);

        m_uploadFence = &fence;
        m_uploadFenceValue = fenceValue;
        m_info.resource = m_gpuResource.Get();
        m_info.width = static_cast<std::uint32_t>(metadata.width);
        m_info.height = static_cast<std::uint32_t>(metadata.height);
        m_info.mipLevels = static_cast<std::uint32_t>(metadata.mipLevels);
        m_info.format = metadata.format;
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