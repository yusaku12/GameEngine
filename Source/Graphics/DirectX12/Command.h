#pragma once

#include "Core\CoreDefines.h"
#include "Graphics\DirectX12\Queue.h"

namespace Engine
{
    class DX12Fence;

    /**
     * @brief DirectX 12 コマンドリストの記録状態
     */
    enum class DX12CommandListState : unsigned int
    {
        CLOSED,    //!< 実行可能な Close 済み状態
        RECORDING, //!< コマンドを記録中の状態
    };

    /**
     * @brief Command Allocator と Graphics Command List のライフサイクルを管理するクラス
     * @thread_safety Not thread-safe. Access must be synchronized externally.
     */
    class DX12CommandList
    {
    public:

        DX12CommandList() = default;
        ~DX12CommandList() = default;

        GE_DISABLE_COPY_AND_MOVE(DX12CommandList);

        /**
         * @brief Command Allocator と Command List を作成し、Close 済み状態にする
         * @param device 作成先デバイス
         * @param type Command List の種別
         * @return 作成に成功した場合は true
         */
        bool initialize(ID3D12Device& device, DX12CommandQueueType type);

        /**
         * @brief 保持している Command Allocator と Command List を解放する
         */
        void finalize();

        /**
         * @brief Fence の完了を確認してから Command Allocator を再利用し、記録を開始する
         * @param completionFence この Command List を提出した Queue の Fence
         * @return 記録を開始できた場合は true
         */
        bool begin(const DX12Fence& completionFence);

        /**
         * @brief コマンド記録を終了し、実行可能な状態にする
         * @return Close に成功した場合は true
         */
        bool close();

        /**
         * @brief Command List が GPU に提出された Fence 値を記録する
         * @param fenceValue 提出後に通知された Fence 値
         * @return 提出状態を記録できた場合は true
         */
        bool markSubmitted(std::uint64_t fenceValue);

        /**
         * @brief 現在の Command List 状態を取得する
         * @return Command List 状態
         */
        DX12CommandListState getState() const noexcept { return m_state; }

        /**
         * @brief Allocator を再利用可能にする Fence 値を取得する
         * @return 最後に提出した Fence 値。未提出時は 0
         */
        std::uint64_t getLastSubmittedFenceValue() const noexcept { return m_lastSubmittedFenceValue; }

        /**
         * @brief 実行可能な Close 済み Command List を取得する
         * @return 非所有の Command List 参照。記録中または未初期化時は nullptr
         */
        ID3D12CommandList* getForExecution() const noexcept;

        /**
         * @brief 記録中の Graphics Command List を取得する
         * @return 非所有の Graphics Command List 参照。Close 済みまたは未初期化時は nullptr
         */
        ID3D12GraphicsCommandList* getForRecording() const noexcept;

    private:

        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocator; //!< Command Allocator
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList; //!< Graphics Command List
        DX12CommandQueueType m_type = DX12CommandQueueType::DIRECT; //!< Command List の種別
        DX12CommandListState m_state = DX12CommandListState::CLOSED; //!< Command List の状態
        std::uint64_t m_lastSubmittedFenceValue = 0; //!< Allocator を再利用可能にする Fence 値
    };
} // namespace Engine
