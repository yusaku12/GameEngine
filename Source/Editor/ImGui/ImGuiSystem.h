#pragma once

#include "Graphics\DirectX12\Descriptor.h"

struct ImGuiContext;
struct ImGui_ImplDX12_InitInfo;

namespace Engine
{
    class EditorUi;
    class ShaderManager;

    /**
     * @brief Dear ImGuiのContextと公式Backendのライフサイクルを管理するクラス
     * @thread_safety Main thread only.
     */
    class ImGuiSystem
    {
    public:
        ImGuiSystem();
        ~ImGuiSystem();

        GE_DISABLE_COPY_AND_MOVE(ImGuiSystem);

        /**
         * @brief Win32/DX12 Backendと日本語フォントを初期化する
         * @param device 既存のDirectX 12 Device
         * @param commandQueue 既存のDirect Command Queue
         * @param hwnd メインWindowのハンドル
         * @param japaneseFontPath 日本語フォントのパス。空の場合はWindowsのMeiryoを探す
         * @return 初期化に成功した場合はtrue
         */
        bool initialize(ID3D12Device& device, ID3D12CommandQueue& commandQueue, HWND hwnd, const std::wstring& japaneseFontPath = {});

        /**
         * @brief BackendとContextを逆順に終了する
         */
        void finalize();

        /**
         * @brief ImGuiのフレームを開始し、Editor UIを生成する
         * @param shaderManager ShaderManager オブジェクトのポインタ (省略可能)
         */
        void beginFrame(ShaderManager* shaderManager = nullptr);

        /**
         * @brief 記録中のCommandListへImGuiを描画する
         * @param commandList 記録中のGraphics CommandList
         */
        void render(ID3D12GraphicsCommandList& commandList);

        /**
         * @brief Win32メッセージをImGuiへ転送する
         * @return ImGuiがメッセージを処理した場合はtrue
         */
        bool processMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

        /**
         * @brief 初期化済みかを取得する
         * @return 初期化済みの場合はtrue
         */
        bool isInitialized() const noexcept { return m_initialized; }

    private:

        /**
         * @brief ImGui_ImplDX12_InitInfoのコールバックで呼び出されるDescriptor割り当て関数
         * @param info ImGui_ImplDX12_InitInfo
         * @param cpuHandle 割り当てたCPU Handleを格納する変数へのポインタ
         * @param gpuHandle 割り当てたGPU Handleを格納する変数へのポインタ
         */
        static void allocateDescriptor(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);

        /**
         * @brief ImGui_ImplDX12_InitInfoのコールバックで呼び出されるDescriptor解放関数
         * @param info ImGui_ImplDX12_InitInfo
         * @param cpuHandle 解放するCPU Handle
         * @param gpuHandle 解放するGPU Handle
         */
        static void freeDescriptor(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

        /**
         * @brief ImGuiのStyleを設定する
         */
        void setupStyle();

        /**
         * @brief 日本語フォントを探す
         * @param requestedPath 指定された日本語フォントのパス
         * @return 見つかった日本語フォントのパス。見つからなかった場合は空文字列
         */
        std::wstring findJapaneseFont(const std::wstring& requestedPath) const;

        ImGuiContext* m_context = nullptr;    //<!< ImGuiのContext
        DX12DescriptorHeap m_srvHeap;         //<!< ImGuiが使用するShader VisibleなSRV Descriptor Heap
        bool m_initialized = false;           //<!< 初期化済みか
        std::unique_ptr<EditorUi> m_editorUi; //<!< Editor UIの描画を担当するクラス
    };
} // namespace Engine
