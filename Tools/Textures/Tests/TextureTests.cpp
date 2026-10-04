#include "Pch.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <d3d12sdklayers.h>
#include "Graphics\Texture\TextureImageUtils.h"
#include "Graphics\Texture\TextureManager.h"
#include "Graphics\DirectX12\Queue.h"
#include "Graphics\DirectX12\Fence.h"

namespace
{
    int checks = 0;

    void require(const bool condition, const char* message)
    {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    }

    void succeeded(const HRESULT result, const char* message)
    {
        if (FAILED(result))
            std::cerr << message << ": HRESULT 0x" << std::hex << result << std::dec << '\n';
        require(SUCCEEDED(result), message);
    }

    DirectX::ScratchImage makeImage(const size_t width = 16, const size_t height = 16)
    {
        DirectX::ScratchImage image;
        succeeded(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1), "Initialize RGBA");
        const auto& pixels = *image.GetImage(0, 0, 0);
        for (size_t y = 0; y < height; ++y)
        {
            for (size_t x = 0; x < width; ++x)
            {
                auto* pixel = pixels.pixels + y * pixels.rowPitch + x * 4;
                pixel[0] = static_cast<std::uint8_t>(x * 255 / std::max<size_t>(1, width - 1));
                pixel[1] = static_cast<std::uint8_t>(y * 255 / std::max<size_t>(1, height - 1));
                pixel[2] = 128;
                pixel[3] = static_cast<std::uint8_t>((x + y) % 3 == 0 ? 0 : 255);
            }
        }
        return image;
    }

    void saveDds(const DirectX::ScratchImage& image, const std::filesystem::path& path)
    {
        succeeded(DirectX::SaveToDDSFile(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
            DirectX::DDS_FLAGS_FORCE_DX10_EXT, path.c_str()), "Save DDS");
    }

    void generate(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root);
        auto image = makeImage();
        succeeded(DirectX::SaveToWICFile(*image.GetImages(), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), (root / L"source image.png").c_str()), "Save PNG");
        for (size_t pixel = 0; pixel < 16 * 16; ++pixel)
            image.GetPixels()[pixel * 4 + 3] = 255;
        succeeded(DirectX::SaveToWICFile(*image.GetImages(), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), (root / L"opaque.png").c_str()), "Save opaque PNG");
        auto odd = makeImage(7, 9);
        succeeded(DirectX::SaveToWICFile(*odd.GetImages(), DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), (root / L"odd.png").c_str()), "Save odd PNG");
        DirectX::ScratchImage hdr;
        succeeded(hdr.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, 16, 16, 1, 1), "Initialize HDR fixture");
        constexpr std::array<float, 4> hdrColor{ 4, 2, 0.5f, 1 };
        for (size_t pixel = 0; pixel < 16 * 16; ++pixel)
        {
            auto* destination = hdr.GetPixels() + pixel * sizeof(hdrColor);
            std::memcpy(destination, hdrColor.data(), sizeof(hdrColor));
        }
        succeeded(DirectX::SaveToHDRFile(*hdr.GetImages(), (root / L"source.hdr").c_str()), "Save HDR");
        std::cout << "Generated texture fixtures.\n";
    }

    void cpuTests(const std::filesystem::path& root)
    {
        using namespace Engine;
        auto image = makeImage();
        const auto originalPixels = std::vector<std::uint8_t>(image.GetPixels(), image.GetPixels() + image.GetPixelsSize());
        TextureLoadDesc desc;
        desc.colorSpace = TextureColorSpace::SRGB;
        succeeded(TextureImageUtils::prepare(image, desc), "Prepare sRGB mips");
        require(image.GetMetadata().format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "sRGB format");
        require(image.GetMetadata().mipLevels == 5, "Complete mip chain");
        require(std::memcmp(image.GetImage(0, 0, 0)->pixels, originalPixels.data(), originalPixels.size()) == 0,
            "Base level preserved");

        auto gamma = makeImage(2, 2);
        for (size_t pixel = 0; pixel < 4; ++pixel)
        {
            auto* channels = gamma.GetPixels() + pixel * 4;
            channels[0] = channels[1] = channels[2] = static_cast<std::uint8_t>(pixel % 2 == 0 ? 0 : 255);
            channels[3] = 255;
        }
        succeeded(TextureImageUtils::prepare(gamma, desc), "Generate gamma-correct mip");
        const auto mipRed = gamma.GetImage(1, 0, 0)->pixels[0];
        require(mipRed >= 186 && mipRed <= 190, "sRGB black/white mip filters in linear space");

        DirectX::ScratchImage empty;
        require(FAILED(TextureImageUtils::prepare(empty, desc)), "Reject empty image");
        DirectX::ScratchImage array;
        succeeded(array.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 2, 1), "Create array");
        require(FAILED(TextureImageUtils::prepare(array, desc)), "Reject unsupported array");
        require(array.GetMetadata().arraySize == 2, "Failed preparation keeps input");
        DirectX::ScratchImage volume;
        succeeded(volume.Initialize3D(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 4, 1), "Create volume");
        require(FAILED(TextureImageUtils::prepare(volume, desc)), "Reject unsupported volume");
        DirectX::ScratchImage typeless;
        succeeded(typeless.Initialize2D(DXGI_FORMAT_BC7_TYPELESS, 4, 4, 1, 1), "Create typeless texture");
        require(FAILED(TextureImageUtils::prepare(typeless, desc)), "Reject unsupported typeless texture");

        const std::array formats{
            DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC7_UNORM,
            DXGI_FORMAT_B8G8R8X8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
        for (const auto format : formats)
        {
            DirectX::ScratchImage tagged;
            succeeded(tagged.Initialize2D(format, 4, 4, 1, 1), "Initialize format");
            std::memset(tagged.GetPixels(), 0, tagged.GetPixelsSize());
            desc.generateMips = false;
            desc.colorSpace = TextureColorSpace::SRGB;
            succeeded(TextureImageUtils::prepare(tagged, desc), "Tag sRGB");
            require(tagged.GetMetadata().format == DirectX::MakeSRGB(format), "All supported sRGB variants");
            desc.colorSpace = TextureColorSpace::Linear;
            succeeded(TextureImageUtils::prepare(tagged, desc), "Tag Linear");
            require(tagged.GetMetadata().format == format, "All supported Linear variants");
        }

        for (const auto& [name, format] : std::array{
            std::pair{ L"color.dds", DXGI_FORMAT_BC7_UNORM_SRGB },
            std::pair{ L"opaque.dds", DXGI_FORMAT_BC7_UNORM_SRGB },
            std::pair{ L"data.dds", DXGI_FORMAT_BC7_UNORM },
            std::pair{ L"mask.dds", DXGI_FORMAT_BC4_UNORM },
            std::pair{ L"ui.dds", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
            std::pair{ L"hdr.dds", DXGI_FORMAT_BC6H_SF16 } })
        {
            DirectX::ScratchImage loaded;
            succeeded(DirectX::LoadFromDDSFile((root / name).c_str(), DirectX::DDS_FLAGS_NONE, nullptr, loaded), "Load cooked DDS");
            require(loaded.GetMetadata().format == format, "Cooked format");
            const size_t expectedMips = format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ? 1 : 5;
            require(loaded.GetMetadata().mipLevels == expectedMips, "Cooked mip count");
            std::uint64_t size = 0;
            succeeded(TextureImageUtils::dataSize(loaded.GetMetadata(), size), "Cooked payload size");
            require(size == loaded.GetPixelsSize(), "Payload size matches actual bytes");
            if (DirectX::IsCompressed(format))
                require(size < 16 * 16 * 4, "Compression reduces pixel payload");
            if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            {
                const auto reference = makeImage();
                require(loaded.GetPixelsSize() == reference.GetPixelsSize(), "UI pixel size");
                require(std::memcmp(loaded.GetPixels(), reference.GetPixels(), reference.GetPixelsSize()) == 0,
                    "Uncompressed UI preserves exact color and alpha bytes");
            }

            if (format == DXGI_FORMAT_BC6H_SF16)
            {
                DirectX::ScratchImage decoded;
                succeeded(DirectX::Decompress(loaded.GetImages(), loaded.GetImageCount(), loaded.GetMetadata(),
                    DXGI_FORMAT_R32G32B32A32_FLOAT, decoded), "Decode HDR");
                std::array<float, 4> channels{};
                std::memcpy(channels.data(), decoded.GetPixels() + 15 * sizeof(channels), sizeof(channels));
                std::cout << "HDR decoded corner: " << channels[0] << ", " << channels[1] << ", " << channels[2] << '\n';
                require(channels[0] >= 3.9f && channels[0] <= 4.1f, "BC6H preserves values above one");
                require(channels[1] >= 1.9f && channels[1] <= 2.1f, "BC6H preserves second HDR channel");
            }
            if (format == DXGI_FORMAT_BC4_UNORM)
            {
                DirectX::ScratchImage decoded;
                succeeded(DirectX::Decompress(loaded.GetImages(), loaded.GetImageCount(), loaded.GetMetadata(),
                    DXGI_FORMAT_R8_UNORM, decoded), "Decode mask");
                for (size_t x = 0; x < 16; ++x)
                {
                    const int error = static_cast<int>(decoded.GetPixels()[x]) - static_cast<int>(x * 255 / 15);
                    require(std::abs(error) <= 4, "BC4 stores the linear red channel");
                }
            }
            if (format == DXGI_FORMAT_BC7_UNORM_SRGB)
            {
                const auto bytes = std::vector<std::uint8_t>(loaded.GetPixels(), loaded.GetPixels() + loaded.GetPixelsSize());
                const auto* originalBuffer = loaded.GetPixels();
                desc = {};
                succeeded(TextureImageUtils::prepare(loaded, desc), "Auto keeps BC7 and existing mips");
                require(loaded.GetMetadata().format == format && loaded.GetMetadata().mipLevels == 5, "Keep metadata");
                require(std::memcmp(bytes.data(), loaded.GetPixels(), bytes.size()) == 0, "Keep all compressed bytes");
                require(loaded.GetPixels() == originalBuffer, "Cooked BC textures avoid a full pixel copy");
                desc.allowCompression = false;
                succeeded(TextureImageUtils::prepare(loaded, desc), "Explicit decompression");
                require(!DirectX::IsCompressed(loaded.GetMetadata().format), "Decompressed format");
                require(DirectX::IsSRGB(loaded.GetMetadata().format), "Decompression preserves sRGB");
                require(loaded.GetMetadata().mipLevels == 5, "Decompression preserves mips");
                auto reference = makeImage();
                const bool opaque = std::wstring_view(name) == L"opaque.dds";
                if (opaque)
                {
                    for (size_t pixel = 0; pixel < 16 * 16; ++pixel)
                        reference.GetPixels()[pixel * 4 + 3] = 255;
                }
                const auto* decoded = loaded.GetImage(0, 0, 0);
                double squaredError = 0;
                std::array<double, 4> channelError{};
                for (size_t y = 0; y < 16; ++y)
                {
                    for (size_t x = 0; x < 16; ++x)
                    {
                        for (size_t channel = 0; channel < 4; ++channel)
                        {
                            const double error = static_cast<double>(decoded->pixels[y * decoded->rowPitch + x * 4 + channel])
                                - reference.GetImages()->pixels[y * reference.GetImages()->rowPitch + x * 4 + channel];
                            squaredError += error * error;
                            channelError[channel] += error * error;
                        }
                    }
                }
                const double mse = squaredError / (16 * 16 * 4);
                const double psnr = mse == 0 ? 100 : 10 * std::log10(255 * 255 / mse);
                std::cout << (opaque ? "Opaque gradient" : "Alpha checkerboard") << " BC7 PSNR: " << psnr << " dB\n";
                std::cout << "Alpha MSE: " << channelError[3] / 256 << '\n';
                require(psnr >= (opaque ? 35 : 25), "BC7 quality threshold for the fixture");
                if (!opaque)
                    require(channelError[3] == 0, "Binary checkerboard alpha exact");
            }
        }

        DirectX::ScratchImage single;
        succeeded(DirectX::LoadFromDDSFile((root / L"single.dds").c_str(), DirectX::DDS_FLAGS_NONE, nullptr, single), "Load one-mip BC7");
        desc = {};
        require(TextureImageUtils::prepare(single, desc) == S_FALSE, "Compressed missing mips reported, not regenerated");
        require(single.GetMetadata().mipLevels == 1, "One mip kept");
        desc.allowCompression = false;
        succeeded(TextureImageUtils::prepare(single, desc), "Decompress then generate mips");
        require(single.GetMetadata().mipLevels == 5, "Decompressed mip generation");

        DirectX::ScratchImage cube;
        succeeded(cube.InitializeCube(DXGI_FORMAT_BC7_UNORM_SRGB, 4, 4, 1, 3), "Initialize BC cube");
        std::memset(cube.GetPixels(), 0, cube.GetPixelsSize());
        desc = {};
        succeeded(TextureImageUtils::prepare(cube, desc), "Keep cube faces and mips");
        std::uint64_t size = 0;
        succeeded(TextureImageUtils::dataSize(cube.GetMetadata(), size), "Cube payload size");
        require(size == 288 && size == cube.GetPixelsSize(), "BC tails use complete blocks on all six faces");
        saveDds(cube, root / L"cube.dds");

        for (const auto format : { DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC7_UNORM })
        {
            DirectX::ScratchImage tiny;
            succeeded(tiny.Initialize2D(format, 1, 1, 1, 1), "Tiny BC image");
            succeeded(TextureImageUtils::dataSize(tiny.GetMetadata(), size), "Tiny BC size");
            require(size == (format == DXGI_FORMAT_BC7_UNORM ? 16u : 8u), "Tiny mip occupies one block");
        }
        require(FAILED(TextureImageUtils::dataSize({}, size)), "Reject invalid size metadata");
        desc = {};
        require(desc == TextureLoadDesc{}, "Equal cache options");
        desc.allowCompression = false;
        require(!(desc == TextureLoadDesc{}), "Different cache options");
    }

    void gpuTests(const std::filesystem::path& root)
    {
        using namespace Engine;
        DX12Device device;
        require(device.initialize({ .enableDebugLayer = true }), "Create debug D3D12 device");
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        succeeded(device.get()->QueryInterface(IID_PPV_ARGS(&messages)), "Get debug messages");
        messages->ClearStoredMessages();
        DX12CommandQueue queue;
        DX12Fence fence;
        require(queue.initialize(*device.get(), DX12CommandQueueType::DIRECT), "Create queue");
        require(fence.initialize(*device.get()), "Create fence");
        auto& manager = TextureManager::instance();
        struct ManagerCleanup
        {
            TextureManager& manager;
            ~ManagerCleanup() { manager.finalize(); }
        } cleanup{ manager };
        require(manager.initialize(device, queue, fence, 64), "Initialize real texture manager");

        for (const auto name : { L"color.dds", L"data.dds", L"mask.dds", L"ui.dds", L"hdr.dds", L"cube.dds" })
        {
            TextureLoadDesc loadDesc;
            loadDesc.generateMips = std::wstring_view(name) != L"ui.dds";
            const auto handle = manager.load(root / name, loadDesc);
            require(handle.isValid(), "Upload cooked DDS");
            const auto* texture = manager.get(handle);
            require(texture != nullptr && texture->isLoaded(), "Uploaded texture ready");
            require(texture->getResourceInfo()->resource->GetDesc().Format == texture->getFormat(), "Resource and SRV format agree");
            require(texture->getGPUMemorySize() > 0, "Nonzero texture payload");
            if (std::wstring_view(name) == L"ui.dds")
                require(texture->getMipLevels() == 1, "UI retains one mip when generation is disabled");
            if (std::wstring_view(name) == L"cube.dds")
                require(texture->getGPUMemorySize() == 288 && texture->getType() == TextureType::TextureCube, "Cube accounting");
            manager.unload(handle);
        }

        const auto path = root / L"color.dds";
        const auto compressed = manager.load(path);
        require(compressed == manager.load(path), "Same options reuse cache");
        TextureLoadDesc desc;
        desc.allowCompression = false;
        const auto uncompressed = manager.load(path, desc);
        require(uncompressed.isValid() && uncompressed != compressed, "Different compression settings use separate cache");
        require(!DirectX::IsCompressed(manager.get(uncompressed)->getFormat()), "Manager honors decompression");
        desc = {};
        desc.colorSpace = TextureColorSpace::Linear;
        const auto linear = manager.load(path, desc);
        require(linear.isValid() && linear != compressed, "Different color space uses separate cache");
        require(manager.get(linear)->getFormat() == DXGI_FORMAT_BC7_UNORM, "Linear resource created before SRV");
        require(manager.exists(path), "Any option variant counts as loaded");
        manager.unload(compressed);
        require(manager.load(path, desc) == linear, "Unloading one option retains other cached variants");
        manager.unload(linear);
        manager.unload(uncompressed);
        require(!manager.exists(path), "All variants unloaded");

        Texture png;
        DX12DescriptorHeap heap;
        require(heap.initialize(*device.get(), { .capacity = 8, .shaderVisible = true }), "Create test SRV heap");
        desc = {};
        desc.colorSpace = TextureColorSpace::SRGB;
        require(png.initialize(*device.get(), heap, queue, fence, root / L"source image.png", desc), "Legacy PNG still loads");
        require(png.getMipLevels() == 5 && DirectX::IsSRGB(png.getFormat()), "Legacy PNG explicit sRGB mips");
        png.finalize();
        require(png.initializeSolidColor(*device.get(), heap, queue, fence, { 255, 255, 255, 255 },
            TextureColorSpace::SRGB), "Solid sRGB texture");
        png.finalize();
        manager.finalize();
        require(fence.waitOnCpu(fence.signal(*queue.get())), "Wait for all uploads");
        for (UINT64 index = 0; index < messages->GetNumStoredMessages(); ++index)
        {
            SIZE_T bytes = 0;
            succeeded(messages->GetMessage(index, nullptr, &bytes), "Get message size");
            auto storage = std::make_unique<std::byte[]>(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.get());
            succeeded(messages->GetMessage(index, message, &bytes), "Read debug message");
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            {
                std::cerr << message->pDescription << '\n';
                require(false, "D3D12 debug-layer error");
            }
        }
        heap.finalize();
        fence.finalize();
        queue.finalize();
        device.finalize();
    }
}

int wmain(const int argc, wchar_t** argv)
{
    if (argc < 2 || argc > 3 || (argc == 3
        && std::wstring_view(argv[2]) != L"--generate" && std::wstring_view(argv[2]) != L"--gpu"))
    {
        std::cerr << "Usage: TextureTests <fixture-directory> [--generate|--gpu]\n";
        return 1;
    }
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult))
        return 1;
    Engine::LoggerConfig logger;
    logger.useFile = false;
    logger.useDebugOutput = false;
    if (!Engine::Logger::instance().initialize(logger))
    {
        CoUninitialize();
        return 1;
    }
    int result = 0;
    try
    {
        const std::filesystem::path root = argv[1];
        if (argc == 3 && std::wstring_view(argv[2]) == L"--generate")
            generate(root);
        else
        {
            if (argc != 3 || std::wstring_view(argv[2]) != L"--gpu")
                cpuTests(root);
            gpuTests(root);
            std::cout << "PASS: " << checks << " texture checks, including real GPU uploads.\n";
        }
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << '\n';
        result = 1;
    }
    Engine::Logger::instance().finalize();
    CoUninitialize();
    return result;
}
