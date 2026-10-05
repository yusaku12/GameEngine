#pragma once

#include "Graphics\DirectX12\Descriptor.h"
#include <unordered_map>

struct ImGuiContext;
struct ImGui_ImplDX12_InitInfo;

namespace Engine
{
    class EditorUi;
    class ShaderManager;

    /**
     * @brief Dear ImGuiのContextと公式Backendのライフサイクルを管理するクラス
    * @thread_safety Thread-safe. Context access is serialized internally.
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
         * @param gameTextureId Game Viewに表示するImGui Texture Descriptor。
         * @param gameWidth Game Viewの物理ピクセル幅。描画後に必要寸法へ更新する。
         * @param gameHeight Game Viewの物理ピクセル高さ。描画後に必要寸法へ更新する。
         * @param drawAdditionalUi Editor UIの後、ImGui::Render前に追加UIを生成するCallback
         */
        void beginFrame(ShaderManager* shaderManager, std::uint64_t gameTextureId,
            std::uint32_t& gameWidth, std::uint32_t& gameHeight,
            const std::function<void()>& drawAdditionalUi = {});

        /**
         * @brief Game Texture用のSRVを作成または更新する。
         * @param device Descriptor作成に使用するDirectX 12 Device。
         * @param resource 表示するTexture。
         * @return 成功した場合はtrue。
         */
        bool updateGameTextureView(ID3D12Device& device, ID3D12Resource& resource);

        /**
         * @brief Game TextureのImGui Descriptor IDを取得する。
         * @return ImGui Imageに渡すDescriptor ID。
         */
        std::uint64_t getGameTextureId() const noexcept { return m_gameTextureId; }

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

        ImGuiContext* m_context = nullptr;                                  //!< ImGuiのContext
        DX12DescriptorHeap m_srvHeap;                                       //!< ImGuiが使用するShader VisibleなSRV Descriptor Heap
        DX12CpuDescriptorHandle m_gameTextureCpuHandle{};                   //!< Game TextureのSRV CPU Handle
        DX12GpuDescriptorHandle m_gameTextureGpuHandle{};                   //!< Game TextureのSRV GPU Handle
        std::unordered_map<SIZE_T, std::uint32_t> m_imguiDescriptorIndices; //!< ImGuiが割り当てたDescriptor slot
        std::uint64_t m_gameTextureId = 0;                                  //!< ImGui Image用Game Texture Descriptor ID
        std::recursive_mutex m_contextMutex;                                //!< WndProc再入を許容するImGui Contextアクセス保護
        bool m_initialized = false;                                         //!< 初期化済みか
        std::unique_ptr<EditorUi> m_editorUi;                               //!< Editor UIの描画を担当するクラス
    };
} // namespace Engine
