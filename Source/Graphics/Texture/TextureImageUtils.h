#pragma once

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <DirectXTex.h>
#include "Graphics\Texture\TextureTypes.h"

namespace Engine::TextureImageUtils
{
    /**
     * @brief Prepare a 2D texture or a single cubemap for upload without runtime compression.
     * @param image Loaded pixels; replaced only after successful processing.
     * @param desc Color-space, decompression and mip-generation settings.
     * @return S_OK on success, S_FALSE if compressed single-level pixels retain no mips,
     *         or a failed HRESULT. S_FALSE is not returned for a 1x1 texture.
     * @thread_safety Independent images may be processed concurrently.
     */
    inline HRESULT prepare(DirectX::ScratchImage& image, const TextureLoadDesc& desc) noexcept
    {
        const auto metadata = image.GetMetadata();
        if (image.GetImageCount() == 0 || metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D
            || metadata.arraySize != (metadata.IsCubemap() ? 6u : 1u)
            || DirectX::IsTypeless(metadata.format) || DirectX::IsPlanar(metadata.format))
            return E_INVALIDARG;

        const bool compressed = DirectX::IsCompressed(metadata.format);
        const bool needsMips = desc.generateMips && metadata.mipLevels == 1
            && (metadata.width > 1 || metadata.height > 1);
        if (desc.allowCompression || !compressed)
        {
            if (!needsMips || compressed)
            {
                DXGI_FORMAT format = metadata.format;
                if (desc.colorSpace == TextureColorSpace::SRGB)
                    format = DirectX::MakeSRGB(format);
                else if (desc.colorSpace == TextureColorSpace::Linear)
                    format = DirectX::MakeLinear(format);
                if (!image.OverrideFormat(format))
                    return E_INVALIDARG;
                return needsMips ? S_FALSE : S_OK;
            }
        }

        DirectX::ScratchImage processed;
        HRESULT result = processed.Initialize(metadata);
        if (FAILED(result))
            return result;
        if (processed.GetImageCount() != image.GetImageCount())
            return E_INVALIDARG;
        for (size_t index = 0; index < image.GetImageCount(); ++index)
        {
            const auto& source = image.GetImages()[index];
            const auto& destination = processed.GetImages()[index];
            if (source.rowPitch != destination.rowPitch || source.slicePitch != destination.slicePitch)
                return E_INVALIDARG;
            std::memcpy(destination.pixels, source.pixels, source.slicePitch);
        }

        if (!desc.allowCompression && DirectX::IsCompressed(metadata.format))
        {
            DirectX::ScratchImage decompressed;
            result = DirectX::Decompress(processed.GetImages(), processed.GetImageCount(),
                processed.GetMetadata(), DXGI_FORMAT_UNKNOWN, decompressed);
            if (FAILED(result))
                return result;
            processed = std::move(decompressed);
        }

        DXGI_FORMAT format = processed.GetMetadata().format;
        if (desc.colorSpace == TextureColorSpace::SRGB)
            format = DirectX::MakeSRGB(format);
        else if (desc.colorSpace == TextureColorSpace::Linear)
            format = DirectX::MakeLinear(format);
        if (!processed.OverrideFormat(format))
            return E_INVALIDARG;

        const auto preparedMetadata = processed.GetMetadata();
        result = S_OK;
        if (desc.generateMips && preparedMetadata.mipLevels == 1
            && (preparedMetadata.width > 1 || preparedMetadata.height > 1))
        {
            if (DirectX::IsCompressed(format))
                result = S_FALSE;
            else
            {
                DirectX::ScratchImage mips;
                result = DirectX::GenerateMipMaps(processed.GetImages(), processed.GetImageCount(),
                    preparedMetadata, DirectX::TEX_FILTER_DEFAULT, 0, mips);
                if (FAILED(result))
                    return result;
                processed = std::move(mips);
            }
        }
        image = std::move(processed);
        return result;
    }

    /**
     * @brief Calculate pixel payload size, including complete BC blocks, slices and mips.
     * @param metadata Metadata of a supported 2D texture or cubemap.
     * @param size Receives bytes on success; not the GPU allocation or upload-buffer size.
     * @return S_OK or a failed HRESULT for invalid metadata/pitch.
     * @thread_safety Thread-safe.
     */
    inline HRESULT dataSize(const DirectX::TexMetadata& metadata, std::uint64_t& size) noexcept
    {
        size = 0;
        if (metadata.width == 0 || metadata.height == 0 || metadata.mipLevels == 0
            || metadata.arraySize == 0 || metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D)
            return E_INVALIDARG;
        size_t width = metadata.width;
        size_t height = metadata.height;
        for (size_t mip = 0; mip < metadata.mipLevels; ++mip)
        {
            size_t rowPitch = 0;
            size_t slicePitch = 0;
            const HRESULT result = DirectX::ComputePitch(metadata.format, width, height, rowPitch, slicePitch);
            if (FAILED(result))
                return result;
            constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
            if (slicePitch > (maximum - size) / metadata.arraySize)
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            size += static_cast<std::uint64_t>(slicePitch) * metadata.arraySize;
            width = std::max<size_t>(1, width / 2);
            height = std::max<size_t>(1, height / 2);
        }
        return S_OK;
    }
}
