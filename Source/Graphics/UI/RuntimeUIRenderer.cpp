#include "Pch.h"
#include "Graphics\UI\RuntimeUIRenderer.h"
#include "UI\RuntimeUI.h"
#include <d3d11on12.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <wincodec.h>
#include "Core\Logging\Logger.h"
#include "Core\Containers\String.h"
#include "Core\Math\MathTypes.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace Engine
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        bool result(const HRESULT hr, const char* operation)
        {
            if (SUCCEEDED(hr)) return true;
            LOG_ERROR("[UI] {} failed: 0x{:08X}", operation, static_cast<std::uint32_t>(hr));
            return false;
        }
        D2D1_RECT_F rect(const UIRect& value) { return { value.min.x, value.min.y, value.max.x, value.max.y }; }
        struct ComApartment
        {
            HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ~ComApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
        };
        struct ClipScope
        {
            ID2D1DeviceContext& context;
            ~ClipScope() { context.PopAxisAlignedClip(); }
        };
    }
    struct RuntimeUIRenderer::Impl
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11On12Device> interop;
        ComPtr<ID2D1Factory1> factory;
        ComPtr<ID2D1Device> d2dDevice;
        ComPtr<ID2D1DeviceContext> draw;
        ComPtr<IDWriteFactory> write;
        ComPtr<ID3D11Resource> wrapped;
        ComPtr<ID2D1Bitmap1> targetBitmap;
        ComPtr<ID2D1SolidColorBrush> brush;
        ID3D12Resource* target = nullptr;
        std::uint64_t frame = 0;
        struct Image
        {
            ComPtr<ID2D1Bitmap1> bitmap;
            ComPtr<ID2D1Effect> tint;
            std::uint64_t lastUsed = 0;
        };
        struct Text
        {
            ComPtr<IDWriteTextLayout> layout;
            std::string content;
            std::string font;
            float size = 0;
            Vector2 dimensions;
            std::uint8_t alignment = 0;
            std::uint64_t lastUsed = 0;
        };
        std::unordered_map<std::filesystem::path, Image> images;
        std::unordered_map<ObjectGUID, Text, ObjectGUIDHash> texts;

        bool bind(ID3D12Resource& resource)
        {
            if (target == &resource) return true;
            D3D11_RESOURCE_FLAGS flags{};
            flags.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (!result(interop->CreateWrappedResource(&resource, &flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_RENDER_TARGET, IID_PPV_ARGS(wrapped.ReleaseAndGetAddressOf())), "Wrap target"))
                return false;
            ComPtr<IDXGISurface> surface;
            if (!result(wrapped.As(&surface), "Get UI surface")) return false;
            const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            if (!result(draw->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
                targetBitmap.ReleaseAndGetAddressOf()), "Create UI target")) return false;
            draw->SetTarget(targetBitmap.Get());
            draw->SetDpi(96, 96);
            target = &resource;
            return true;
        }
        Image* image(const std::filesystem::path& path)
        {
            const auto found = images.find(path);
            if (found != images.end()) { found->second.lastUsed = frame; return &found->second; }
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICImagingFactory> wic;
            ComPtr<IWICBitmapFrameDecode> source;
            ComPtr<IWICFormatConverter> converter;
            Image value;
            if (!result(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(wic.GetAddressOf())), "Create WIC factory")
                || !result(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                decoder.GetAddressOf()), "Decode UI image")
                || !result(decoder->GetFrame(0, source.GetAddressOf()), "Get image frame")
                || !result(wic->CreateFormatConverter(converter.GetAddressOf()), "Create image converter")
                || !result(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert image")
                || !result(draw->CreateBitmapFromWicBitmap(converter.Get(), nullptr, value.bitmap.GetAddressOf()), "Upload UI image")
                || !result(draw->CreateEffect(CLSID_D2D1ColorMatrix, value.tint.GetAddressOf()), "Create tint effect"))
                return nullptr;
            value.tint->SetInput(0, value.bitmap.Get());
            value.lastUsed = frame;
            return &images.emplace(path, std::move(value)).first->second;
        }
        IDWriteTextLayout* text(const UIDraw& item)
        {
            if (item.alignment > 2)
            {
                LOG_ERROR("[UI] Invalid text alignment in a drawing snapshot.");
                return nullptr;
            }
            auto& value = texts[item.object];
            const Vector2 dimensions = item.rect.max - item.rect.min;
            if (!value.layout || value.content != item.text || value.font != item.font || value.size != item.fontSize
                || value.dimensions != dimensions || value.alignment != item.alignment)
            {
                const std::wstring font = String(item.font).toWide();
                const std::wstring content = String(item.text).toWide();
                ComPtr<IDWriteTextFormat> format;
                ComPtr<IDWriteTextLayout> layout;
                constexpr DWRITE_TEXT_ALIGNMENT alignments[] = {
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_TEXT_ALIGNMENT_TRAILING };
                if (!result(write->CreateTextFormat(font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, item.fontSize, L"", format.GetAddressOf()), "Create text format")
                    || !result(write->CreateTextLayout(content.data(), static_cast<UINT32>(content.size()), format.Get(),
                        dimensions.x, dimensions.y, layout.GetAddressOf()), "Shape Unicode text")
                    || !result(layout->SetTextAlignment(alignments[item.alignment]), "Align text"))
                    return nullptr;
                value.layout = std::move(layout);
                value.content = item.text; value.font = item.font; value.size = item.fontSize;
                value.dimensions = dimensions; value.alignment = item.alignment;
            }
            value.lastUsed = frame;
            return value.layout.Get();
        }
    };
    RuntimeUIRenderer::RuntimeUIRenderer() : m_impl(std::make_unique<Impl>()) {}
    RuntimeUIRenderer::~RuntimeUIRenderer() { finalize(); }
    bool RuntimeUIRenderer::initialize(ID3D12Device& device, ID3D12CommandQueue& queue)
    {
        finalize();
        IUnknown* queues[] = { &queue };
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        auto& state = *m_impl;
        if (!result(D3D11On12CreateDevice(&device, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
            queues, 1, 0, state.device.GetAddressOf(), state.context.GetAddressOf(), nullptr), "D3D11On12CreateDevice")
            || !result(state.device.As(&state.interop), "Get D3D11On12 interop")) { finalize(); return false; }
        ComPtr<IDXGIDevice> dxgi;
        const D2D1_FACTORY_OPTIONS options{};
        if (!result(state.device.As(&dxgi), "Get DXGI device")
            || !result(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1),
                &options, reinterpret_cast<void**>(state.factory.GetAddressOf())), "Create D2D factory")
            || !result(state.factory->CreateDevice(dxgi.Get(), state.d2dDevice.GetAddressOf()), "Create D2D device")
            || !result(state.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, state.draw.GetAddressOf()), "Create D2D context")
            || !result(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(state.write.GetAddressOf())), "Create DirectWrite")
            || !result(state.draw->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), state.brush.GetAddressOf()), "Create UI brush"))
        { finalize(); return false; }
        state.draw->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        return true;
    }
    void RuntimeUIRenderer::releaseTarget() noexcept
    {
        if (m_impl->draw) m_impl->draw->SetTarget(nullptr);
        m_impl->targetBitmap.Reset(); m_impl->wrapped.Reset(); m_impl->target = nullptr;
        if (m_impl->context) m_impl->context->Flush();
    }
    void RuntimeUIRenderer::finalize() noexcept
    {
        releaseTarget();
        m_impl->images.clear(); m_impl->texts.clear(); m_impl->brush.Reset();
        m_impl->write.Reset(); m_impl->draw.Reset(); m_impl->d2dDevice.Reset();
        m_impl->factory.Reset(); m_impl->interop.Reset(); m_impl->context.Reset(); m_impl->device.Reset();
    }
    bool RuntimeUIRenderer::render(ID3D12Resource& target, const std::span<const UIDraw> draws)
    {
        if (draws.empty()) return true;
        const ComApartment apartment;
        if (FAILED(apartment.status) && apartment.status != RPC_E_CHANGED_MODE)
            return result(apartment.status, "Initialize rendering apartment");
        auto& state = *m_impl;
        if (!state.interop)
        {
            LOG_ERROR("[UI] Renderer is not initialized.");
            return false;
        }
        if (!state.bind(target)) return false;
        ++state.frame;
        ID3D11Resource* resources[] = { state.wrapped.Get() };
        state.interop->AcquireWrappedResources(resources, 1);
        state.draw->BeginDraw();
        bool succeeded = true;
        try
        {
            for (const auto& item : draws)
            {
                state.draw->SetTransform(D2D1::Matrix3x2F::Identity());
                state.draw->PushAxisAlignedClip(rect(item.clip), D2D1_ANTIALIAS_MODE_ALIASED);
                const ClipScope clip{ *state.draw.Get() };
                state.brush->SetColor(D2D1::ColorF(item.color.x, item.color.y, item.color.z, item.color.w));
                if (item.kind == UIDraw::Kind::Text)
                {
                    IDWriteTextLayout* layout = state.text(item);
                    if (!layout) succeeded = false;
                    else state.draw->DrawTextLayout(D2D1::Point2F(item.rect.min.x, item.rect.min.y),
                        layout, state.brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
                else if (item.image.empty())
                    state.draw->FillRectangle(rect(item.rect), state.brush.Get());
                else
                {
                    auto* image = state.image(item.image);
                    if (!image) succeeded = false;
                    else
                    {
                        const auto size = image->bitmap->GetSize();
                        const D2D1_MATRIX_5X4_F tint = {
                            item.color.x, 0, 0, 0, 0, item.color.y, 0, 0,
                            0, 0, item.color.z, 0, 0, 0, 0, item.color.w, 0, 0, 0, 0 };
                        if (!result(image->tint->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, tint), "Apply image tint"))
                            succeeded = false;
                        state.draw->SetTransform(D2D1::Matrix3x2F::Scale(
                            (item.rect.max.x - item.rect.min.x) / size.width,
                            (item.rect.max.y - item.rect.min.y) / size.height)
                            * D2D1::Matrix3x2F::Translation(item.rect.min.x, item.rect.min.y));
                        state.draw->DrawImage(image->tint.Get());
                    }
                }
                if (!succeeded) break;
            }
        }
        catch (const std::bad_alloc&) { LOG_ERROR("[UI] Insufficient memory for drawing resources."); succeeded = false; }
        state.draw->SetTransform(D2D1::Matrix3x2F::Identity());
        const HRESULT end = state.draw->EndDraw();
        state.interop->ReleaseWrappedResources(resources, 1);
        state.context->Flush();
        std::erase_if(state.images, [&state](const auto& entry) { return state.frame - entry.second.lastUsed > 120; });
        std::erase_if(state.texts, [&state](const auto& entry) { return state.frame - entry.second.lastUsed > 120; });
        return result(end, "Draw Runtime UI") && succeeded;
    }
}
