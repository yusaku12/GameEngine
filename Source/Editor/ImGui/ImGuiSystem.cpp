#include "Pch.h"
#include "Editor\ImGui\ImGuiSystem.h"
#include "Editor\ImGui\EditorUi.h"
#include <imgui.h>
#include <backends\imgui_impl_dx12.h>
#include <backends\imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

namespace Engine
{
    ImGuiSystem::ImGuiSystem() = default;

    ImGuiSystem::~ImGuiSystem()
    {
        finalize();
    }

    bool ImGuiSystem::initialize(
        ID3D12Device& device,
        ID3D12CommandQueue& commandQueue,
        const HWND hwnd,
        const std::wstring& japaneseFontPath)
    {
        if (m_initialized || hwnd == nullptr)
            return false;

        DX12DescriptorHeapConfig heapConfig{};
        heapConfig.type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapConfig.capacity = 64;
        heapConfig.shaderVisible = true;
        if (!m_srvHeap.initialize(device, heapConfig))
            return false;

        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        if (m_context == nullptr)
        {
            m_srvHeap.finalize();
            return false;
        }
        ImGui::SetCurrentContext(m_context);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        std::error_code directoryError;
        std::filesystem::create_directories("Config/ImGui", directoryError);
        io.IniFilename = "Config/ImGui/imgui.ini";

        const std::wstring fontPath = findJapaneseFont(japaneseFontPath);
        if (!fontPath.empty())
        {
            const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, fontPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Length > 0)
            {
                std::string utf8FontPath(static_cast<std::size_t>(utf8Length), '\0');
                WideCharToMultiByte(CP_UTF8, 0, fontPath.c_str(), -1, utf8FontPath.data(), utf8Length, nullptr, nullptr);
                ImFont* japaneseFont = io.Fonts->AddFontFromFileTTF(utf8FontPath.c_str(), 16.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
                if (japaneseFont != nullptr)
                    io.FontDefault = japaneseFont;
            }
        }

        if (!ImGui_ImplWin32_Init(hwnd))
        {
            finalize();
            return false;
        }

        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = &device;
        initInfo.CommandQueue = &commandQueue;
        initInfo.NumFramesInFlight = 2;
        initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.UserData = this;
        initInfo.SrvDescriptorHeap = m_srvHeap.get();
        initInfo.SrvDescriptorAllocFn = &ImGuiSystem::allocateDescriptor;
        initInfo.SrvDescriptorFreeFn = &ImGuiSystem::freeDescriptor;
        if (!ImGui_ImplDX12_Init(&initInfo))
        {
            finalize();
            return false;
        }

        setupStyle();
        m_editorUi = std::make_unique<EditorUi>();
        m_initialized = true;
        return true;
    }

    void ImGuiSystem::finalize()
    {
        if (m_context != nullptr)
        {
            ImGui::SetCurrentContext(m_context);
            if (m_initialized)
                ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext(m_context);
            m_context = nullptr;
        }
        m_editorUi.reset();
        m_srvHeap.finalize();
        m_initialized = false;
    }

    void ImGuiSystem::beginFrame(ShaderManager* shaderManager)
    {
        if (!m_initialized || m_context == nullptr)
            return;

        ImGui::SetCurrentContext(m_context);
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        m_editorUi->draw(shaderManager);
        ImGui::Render();
    }

    void ImGuiSystem::render(ID3D12GraphicsCommandList& commandList)
    {
        if (!m_initialized || m_context == nullptr)
            return;

        ImGui::SetCurrentContext(m_context);
        ID3D12DescriptorHeap* const heap = m_srvHeap.get();
        commandList.SetDescriptorHeaps(1, &heap);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), &commandList);
    }

    bool ImGuiSystem::processMessage(const HWND hwnd, const UINT message, const WPARAM wparam, const LPARAM lparam)
    {
        if (!m_initialized || m_context == nullptr)
            return false;

        ImGui::SetCurrentContext(m_context);
        return ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam) != 0;
    }

    void ImGuiSystem::allocateDescriptor(
        ::ImGui_ImplDX12_InitInfo* const info,
        D3D12_CPU_DESCRIPTOR_HANDLE* const cpuHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE* const gpuHandle)
    {
        if (info == nullptr || cpuHandle == nullptr || gpuHandle == nullptr || info->UserData == nullptr)
            return;

        ImGuiSystem& system = *static_cast<ImGuiSystem*>(info->UserData);
        const std::optional<DX12DescriptorAllocation> allocation = system.m_srvHeap.allocate();
        if (!allocation.has_value() || !allocation->gpu.has_value())
            return;

        *cpuHandle = allocation->cpu.native;
        *gpuHandle = allocation->gpu->native;
    }

    void ImGuiSystem::freeDescriptor(
        ::ImGui_ImplDX12_InitInfo* const info,
        const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
    {
        (void)info;
        (void)cpuHandle;
        (void)gpuHandle;
    }

    void ImGuiSystem::setupStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::StyleColorsDark(&style);
        style.WindowRounding = 3.0f;
        style.ChildRounding = 2.0f;
        style.FrameRounding = 2.0f;
        style.PopupRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;
        style.GrabRounding = 2.0f;
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.WindowPadding = ImVec2(10.0f, 8.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.085f, 0.095f, 1.0f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.060f, 0.068f, 0.078f, 1.0f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.095f, 0.105f, 0.120f, 1.0f);
        colors[ImGuiCol_Border] = ImVec4(0.20f, 0.24f, 0.27f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.16f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.29f, 0.31f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.40f, 0.41f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.12f, 0.25f, 0.27f, 1.0f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.16f, 0.37f, 0.38f, 1.0f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.19f, 0.47f, 0.45f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.13f, 0.31f, 0.32f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.43f, 0.42f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.54f, 0.50f, 1.0f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.85f, 0.70f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.78f, 0.68f, 1.0f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.93f, 0.78f, 1.0f);
        colors[ImGuiCol_Tab] = ImVec4(0.10f, 0.20f, 0.22f, 1.0f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.16f, 0.39f, 0.39f, 1.0f);
        colors[ImGuiCol_TabActive] = ImVec4(0.14f, 0.31f, 0.32f, 1.0f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.23f, 0.24f, 1.0f);
    }

    std::wstring ImGuiSystem::findJapaneseFont(const std::wstring& requestedPath) const
    {
        if (!requestedPath.empty() && std::filesystem::exists(requestedPath))
            return requestedPath;

        wchar_t windowsDirectory[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"WINDIR", windowsDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        const std::filesystem::path fontPath = std::filesystem::path(windowsDirectory) / L"Fonts" / L"meiryo.ttc";
        return std::filesystem::exists(fontPath) ? fontPath.wstring() : std::wstring{};
    }
} // namespace Engine