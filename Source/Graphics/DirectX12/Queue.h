#pragma once

#include <span>

#include <d3d12.h>
#include <wrl/client.h>

#include "Core\CoreDefines.h"

namespace Engine
{
	/**
	 * @brief DirectX 12 コマンドキューの種別
	 */
	enum class DX12CommandQueueType : unsigned int
	{
		DIRECT = D3D12_COMMAND_LIST_TYPE_DIRECT,   //!< 描画と汎用コマンド
		COMPUTE = D3D12_COMMAND_LIST_TYPE_COMPUTE, //!< Compute コマンド
		COPY = D3D12_COMMAND_LIST_TYPE_COPY,       //!< Copy コマンド
	};

	/**
	 * @brief DirectX 12 コマンドキューを所有してコマンドリストを実行するクラス
	 * @thread_safety Not thread-safe. Access must be synchronized externally.
	 */
	class DX12CommandQueue
	{
	public:

		DX12CommandQueue() = default;
		~DX12CommandQueue() = default;

		GE_DISABLE_COPY_AND_MOVE(DX12CommandQueue);

		/**
		 * @brief 指定種別のコマンドキューを作成する
		 * @param device コマンドキューを作成するデバイス
		 * @param type コマンドキューの種別
		 * @return 作成に成功した場合は true
		 */
		bool initialize(ID3D12Device& device, DX12CommandQueueType type);

		/**
		 * @brief 保持しているコマンドキューを解放する
		 */
		void finalize();

		/**
		 * @brief Close 済みのコマンドリストを GPU に実行登録する
		 * @param commandLists 実行するコマンドリスト
		 */
		void execute(std::span<ID3D12CommandList* const> commandLists) const;

		/**
		 * @brief コマンドキューの種別を取得する
		 * @return コマンドキューの種別
		 */
		[[nodiscard]] DX12CommandQueueType getType() const noexcept { return m_type; }

		/**
		 * @brief DirectX 12 コマンドキューを取得する
		 * @return 非所有のコマンドキュー参照。未初期化時は nullptr
		 */
		[[nodiscard]] ID3D12CommandQueue* get() const noexcept { return m_queue.Get(); }

	private:

		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue; //!< DirectX 12 コマンドキュー
		DX12CommandQueueType m_type = DX12CommandQueueType::DIRECT; //!< コマンドキューの種別
	};
} // namespace Engine
