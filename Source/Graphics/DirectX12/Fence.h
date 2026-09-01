#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "Core\CoreDefines.h"

namespace Engine
{
	/**
	 * @brief GPU コマンドの完了を追跡する DirectX 12 Fence
	 * @thread_safety Not thread-safe. Access must be synchronized externally.
	 */
	class DX12Fence
	{
	public:

		DX12Fence() = default;
		~DX12Fence();

		GE_DISABLE_COPY_AND_MOVE(DX12Fence);

		/**
		 * @brief Fence と CPU 待機イベントを作成する
		 * @param device Fence を作成するデバイス
		 * @return 作成に成功した場合は true
		 */
		bool initialize(ID3D12Device& device);

		/**
		 * @brief GPU の使用完了を待機して保持リソースを解放する
		 */
		void finalize();

		/**
		 * @brief Queue に Fence 値を通知する
		 * @param queue 通知先のコマンドキュー
		 * @return 通知した Fence 値。失敗時は 0
		 */
		[[nodiscard]] std::uint64_t signal(ID3D12CommandQueue& queue);

		/**
		 * @brief GPU 側で指定 Fence 値の完了を待機する
		 * @param queue 待機を実行するコマンドキュー
		 * @param value 待機する Fence 値
		 * @return 待機コマンドの登録に成功した場合は true
		 */
		bool waitOnGpu(ID3D12CommandQueue& queue, std::uint64_t value) const;

		/**
		 * @brief CPU 側で指定 Fence 値の完了を待機する
		 * @param value 待機する Fence 値
		 * @return 待機に成功した場合は true
		 */
		bool waitOnCpu(std::uint64_t value) const;

		/**
		 * @brief 指定 Fence 値が完了済みかを判定する
		 * @param value 確認する Fence 値
		 * @return 完了済みの場合は true
		 */
		[[nodiscard]] bool isComplete(std::uint64_t value) const noexcept;

		/**
		 * @brief GPU が完了した最新 Fence 値を取得する
		 * @return 最新の完了 Fence 値。未初期化時は 0
		 */
		[[nodiscard]] std::uint64_t getCompletedValue() const noexcept;

		/**
		 * @brief DirectX 12 Fence を取得する
		 * @return 非所有の Fence 参照。未初期化時は nullptr
		 */
		[[nodiscard]] ID3D12Fence* get() const noexcept { return m_fence.Get(); }

	private:

		Microsoft::WRL::ComPtr<ID3D12Fence> m_fence; //!< DirectX 12 Fence
		HANDLE m_event = nullptr; //!< CPU 待機イベント
		std::uint64_t m_nextValue = 1; //!< 次に通知する Fence 値
	};
} // namespace Engine
