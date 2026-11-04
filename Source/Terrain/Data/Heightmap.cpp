#include "Pch.h"
#include "Terrain\Data\Heightmap.h"
#include <DirectXTex.h>
#include <cmath>
#include <cstring>

namespace Engine
{
    namespace
    {
        bool validDimensions(const std::size_t width, const std::size_t height) noexcept
        {
            return width >= 2 && height >= 2
                && width <= Heightmap::MAX_DIMENSION && height <= Heightmap::MAX_DIMENSION;
        }

        struct ComApartment
        {
            HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
        };
    }

    bool Heightmap::assign(const std::uint32_t width, const std::uint32_t height,
        const std::span<const float> samples)
    {
        if (!validDimensions(width, height)
            || samples.size() != static_cast<std::size_t>(width) * height
            || !std::all_of(samples.begin(), samples.end(),
                [](const float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }))
        {
            LOG_ERROR("[Heightmap] Expected a 2..{} grid and finite normalized samples.", MAX_DIMENSION);
            return false;
        }
        std::vector<float> replacement(samples.begin(), samples.end());
        m_samples.swap(replacement);
        m_width = width;
        m_height = height;
        return true;
    }

    bool Heightmap::createFlat(const std::uint32_t width, const std::uint32_t height, const float value)
    {
        if (!validDimensions(width, height) || !std::isfinite(value) || value < 0.0f || value > 1.0f)
        {
            LOG_ERROR("[Heightmap] Invalid flat grid dimensions or normalized height.");
            return false;
        }
        std::vector<float> replacement(static_cast<std::size_t>(width) * height, value);
        m_samples.swap(replacement);
        m_width = width;
        m_height = height;
        return true;
    }

    bool Heightmap::loadImage(const std::filesystem::path& path)
    {
        const ComApartment apartment;
        if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
        {
            LOG_ERROR("[Heightmap] Cannot initialize WIC COM apartment: {}.", apartment.result);
            return false;
        }
        DirectX::TexMetadata metadata{};
        HRESULT result = DirectX::GetMetadataFromWICFile(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, metadata);
        if (FAILED(result) || !validDimensions(metadata.width, metadata.height))
        {
            LOG_ERROR("[Heightmap] Cannot read image or dimensions exceed 2..{}: '{}' ({}).",
                MAX_DIMENSION, path.string(), result);
            return false;
        }

        DirectX::ScratchImage image;
        result = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, &metadata, image);
        const DirectX::Image* source = image.GetImage(0, 0, 0);
        if (FAILED(result) || source == nullptr || !validDimensions(source->width, source->height))
        {
            LOG_ERROR("[Heightmap] Cannot decode image: '{}' ({}).", path.string(), result);
            return false;
        }

        DirectX::ScratchImage converted;
        if (source->format != DXGI_FORMAT_R32_FLOAT)
        {
            result = DirectX::Convert(*source, DXGI_FORMAT_R32_FLOAT,
                DirectX::TEX_FILTER_RGB_COPY_RED, 0.0f, converted);
            if (FAILED(result))
            {
                LOG_ERROR("[Heightmap] Cannot convert image to height samples: '{}' ({}).", path.string(), result);
                return false;
            }
            source = converted.GetImage(0, 0, 0);
        }
        std::vector<float> samples(source->width * source->height);
        for (std::size_t row = 0; row < source->height; ++row)
            std::memcpy(samples.data() + row * source->width,
                source->pixels + row * source->rowPitch, source->width * sizeof(float));
        return assign(static_cast<std::uint32_t>(source->width),
            static_cast<std::uint32_t>(source->height), samples);
    }
}
