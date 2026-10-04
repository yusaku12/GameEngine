#pragma once

#include "Graphics\DirectX12\Command.h"
#include "Graphics\Renderer\CommandRecordingRange.h"
#include "Graphics\Renderer\RenderItem.h"

namespace Engine
{
    class DX12Fence;
    class DX12GraphicsPipeline;
    class DX12OcclusionQueries;

    /**
     * @brief 描画コマンド記録のために準備されたモデル描画データ
     * @details GPUリソースを所有せず、1フレーム内だけ有効な参照と描画状態を保持する。
     */
    struct DX12PreparedModelDraw
    {
        const RenderItem* item = nullptr;                      //!< 描画対象の RenderItem への非所有参照
        const DX12GraphicsPipeline* pipeline = nullptr;        //!< 描画対象の PSO への非所有参照
        std::size_t itemIndex = 0;                             //!< 描画対象の RenderItem のインデックス
        std::uint32_t instanceCount = 0;                       //!< 描画対象の RenderItem のインスタンス数
        D3D12_GPU_VIRTUAL_ADDRESS instances = 0;               //!< 描画対象の RenderItem のインスタンスデータへの GPU 仮想アドレス
        D3D12_GPU_VIRTUAL_ADDRESS material = 0;                //!< 描画対象の RenderItem の MaterialPropertyBlock への GPU 仮想アドレス
        D3D12_GPU_VIRTUAL_ADDRESS bones = 0;                   //!< 描画対象の RenderItem のボーンデータへの GPU 仮想アドレス
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 5> textures{}; //!< 描画対象の RenderItem のテクスチャへの GPU ディスクリプタハンドル
    };

    /**
     * @brief 描画コマンド記録のための状態
     * @details GPUリソースを所有せず、1フレーム内だけ有効な参照と描画状態を保持する。
     */
    struct DX12ModelRecordingState
    {
        ID3D12DescriptorHeap* textureHeap = nullptr;   //!< 描画対象の RenderItem のテクスチャ用ディスクリプタヒープへの非所有参照
        D3D12_CPU_DESCRIPTOR_HANDLE colorTarget{};     //!< 描画対象の RenderItem のカラーレンダーターゲットへの CPU ディスクリプタハンドル
        D3D12_CPU_DESCRIPTOR_HANDLE depthTarget{};     //!< 描画対象の RenderItem の深度レンダーターゲットへの CPU ディスクリプタハンドル
        D3D12_CPU_DESCRIPTOR_HANDLE shadowTarget{};    //!< 描画対象の RenderItem のシャドウレンダーターゲットへの CPU ディスクリプタハンドル
        D3D12_VIEWPORT viewport{};                     //!< 描画対象の RenderItem のビューポート
        D3D12_RECT scissor{};                          //!< 描画対象の RenderItem のシザー矩形
        D3D12_VIEWPORT shadowViewport{};               //!< 描画対象の RenderItem のシャドウマップ用ビューポート
        D3D12_RECT shadowScissor{};                    //!< 描画対象の RenderItem のシャドウマップ用シザー矩形
        const DX12OcclusionQueries* queries = nullptr; //!< 描画対象の RenderItem のオクルージョンクエリへの非所有参照
    };

    /**
     * @brief 描画コマンド記録の統計情報
     */
    struct DX12ModelRecordingStatistics
    {
        std::uint32_t drawCallCount = 0;           //!< 描画コール数
        std::uint32_t instanceCount = 0;           //!< インスタンス描画数
        std::uint32_t psoSwitchCount = 0;          //!< パイプラインステート切り替え数
        std::uint32_t materialSwitchCount = 0;     //!< マテリアル切り替え数
        std::uint32_t textureSwitchCount = 0;      //!< テクスチャ切り替え数
        std::uint32_t vertexBufferSwitchCount = 0; //!< Vertex Buffer 切り替え数
        std::uint32_t indexBufferSwitchCount = 0;  //!< Index Buffer 切り替え数
        std::uint32_t recordingThreadCount = 0;    //!< 描画コマンド記録に使用したスレッド数
    };

    /**
     * @brief モデル描画コマンド記録を行うクラス
     * @details GPUリソースを所有せず、1フレーム内だけ有効な参照と描画状態を保持する。
     */
    class DX12ModelCommandRecorder
    {
    public:
        DX12ModelCommandRecorder() = default;
        GE_DISABLE_COPY_AND_MOVE(DX12ModelCommandRecorder);

        /**
         * @brief 描画コマンド記録を行う
         * @param device コマンド記録に使用する DirectX 12 デバイス
         * @param fence コマンド記録の完了を追跡するフェンス
         * @param draws 描画対象の準備済みモデル描画データの範囲
         * @param state 描画コマンド記録の状態
         * @param parallel 並列記録を有効にするかどうか
         * @return 記録に成功した場合は true
         */
        bool record(ID3D12Device& device, const DX12Fence& fence,
            std::span<const DX12PreparedModelDraw> draws, const DX12ModelRecordingState& state, bool parallel);

        /**
         * @brief GPU 完了後にリソースと CPU 側状態を解放する
         */
        void finalize();

        /**
         * @brief コマンド記録を GPU に提出したフェンス値を記録する
         * @param fenceValue 提出後に通知されたフェンス値
         * @return 提出状態を記録できた場合は true
         */
        bool markSubmitted(std::uint64_t fenceValue);

        /**
         * @brief コマンド記録範囲を取得する
         * @return コマンド記録範囲のスパン
         */
        std::span<const CommandRecordingRange> getRanges() const noexcept { return m_ranges; }

        /**
         * @brief 実行可能な Close 済み Command List を取得する
         * @param index コマンドリストのインデックス
         * @return 非所有の Command List 参照。記録中または未初期化時は nullptr
         */
        ID3D12CommandList* getForExecution(std::size_t index) const noexcept;

        /**
         * @brief 記録中の Graphics Command List を取得する
         * @param index コマンドリストのインデックス
         * @return 非所有の Graphics Command List 参照。Close 済みまたは未初期化時は nullptr
         */
        const DX12ModelRecordingStatistics& getStatistics() const noexcept { return m_statistics; }

    private:

        static constexpr std::size_t MAX_TASKS_PER_PASS = 8;  //!< パスごとの最大タスク数
        static constexpr std::size_t MIN_DRAWS_PER_TASK = 64; //!< タスクごとの最小ターゲット描画数

        /**
         * @brief 描画コマンド記録のためのコンテキスト
         */
        struct Context
        {
            DX12CommandList commandList;             //!< 描画コマンド記録用の DirectX 12 コマンドリスト
            DX12ModelRecordingStatistics statistics; //!< 描画コマンド記録の統計情報
            bool succeeded = false;                  //!< 描画コマンド記録が成功したかどうか
            std::uint32_t threadId = 0;              //!< 描画コマンド記録を実行したスレッドの ID
        };

        /**
         * @brief 描画コマンド記録のための範囲を記録する
         * @param context 描画コマンド記録のコンテキスト
         * @param fence コマンド記録の完了を追跡するフェンス
         * @param draws 描画対象の準備済みモデル描画データの範囲
         * @param state 描画コマンド記録の状態
         * @return 記録に成功した場合は true
         */
        bool recordRange(Context& context, const DX12Fence& fence,
            std::span<const DX12PreparedModelDraw> draws, const DX12ModelRecordingState& state);

        std::vector<std::unique_ptr<Context>> m_contexts; //!< 描画コマンド記録のコンテキストの配列
        std::vector<CommandRecordingRange> m_ranges;      //!< 描画コマンド記録の範囲の配列
        DX12ModelRecordingStatistics m_statistics;        //!< 描画コマンド記録の統計情報
    };
} // namespace Engine
