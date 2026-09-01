#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <d3d12.h>
#include <wrl/client.h>

#include "Core\CoreDefines.h"

namespace Engine
{
	class DX12CommandList;
	class DX12Fence;

	/**
	 * @brief Committed Resource の作成設定
	 */
	struct DX12ResourceConfig
	{
		D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT; //!< Resource を配置する Heap 種別
		D3D12_RESOURCE_DESC description{}; //!< Format、Size、Usage を含む Resource 設定
		D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON; //!< 作成直後の Resource State
		const D3D12_CLEAR_VALUE* clearValue = nullptr; //!< Render Target または Depth Stencil の最適化 Clear 値
	};

	/**
	 * @brief Resource State を追跡する DirectX 12 Committed Resource
	 * @thread_safety Not thread-safe. Access must be synchronized externally.
	 */
	class DX12Resource
	{
	public:

		DX12Resource() = default;
		~DX12Resource() = default;

		GE_DISABLE_COPY_AND_MOVE(DX12Resource);

		/**
		 * @brief 指定設定で Committed Resource を作成する
		 * @param device Resource を作成するデバイス
		 * @param config Heap、Format、Size、Usage、および初期状態の設定
		 * @return 作成に成功した場合は true
		 */
		bool initialize(ID3D12Device& device, const DX12ResourceConfig& config);

		/**
		 * @brief 外部 API が所有する既存 Resource を State 追跡対象として保持する
		 * @param resource 保持する Resource
		 * @param initialState 追跡を開始する Resource State
		 * @return 初期化に成功した場合は true
		 */
		bool initializeExisting(ID3D12Resource& resource, D3D12_RESOURCE_STATES initialState);

		/**
		 * @brief 保持している Resource を解放する
		 * @warning GPU 使用完了を Fence で確認してから呼び出すこと
		 */
		void finalize();

		/**
		 * @brief 記録中の Command List に Resource State 遷移を記録する
		 * @param commandList 遷移を記録する Command List
		 * @param state 遷移先の Resource State
		 * @param subresource 遷移対象の Subresource
		 * @return 遷移を記録できた場合は true
		 */
		bool transition(DX12CommandList& commandList, D3D12_RESOURCE_STATES state, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

		/**
		 * @brief Resource の作成時状態を取得する
		 * @return 作成時状態
		 */
		[[nodiscard]] D3D12_RESOURCE_STATES getInitialState() const noexcept { return m_initialState; }

		/**
		 * @brief 追跡中の現在 Resource State を取得する
		 * @return 現在の Resource State
		 */
		[[nodiscard]] D3D12_RESOURCE_STATES getCurrentState() const noexcept { return m_currentState; }

		/**
		 * @brief Resource の Format、Size、Usage を取得する
		 * @return Resource 設定
		 */
		[[nodiscard]] const D3D12_RESOURCE_DESC& getDescription() const noexcept { return m_description; }

		/**
		 * @brief DirectX 12 Resource を取得する
		 * @return 非所有の Resource 参照。未初期化時は nullptr
		 */
		[[nodiscard]] ID3D12Resource* get() const noexcept { return m_resource.Get(); }

	private:

		Microsoft::WRL::ComPtr<ID3D12Resource> m_resource; //!< DirectX 12 Resource
		D3D12_RESOURCE_DESC m_description{}; //!< Format、Size、Usage を含む Resource 設定
		D3D12_RESOURCE_STATES m_initialState = D3D12_RESOURCE_STATE_COMMON; //!< 作成時 Resource State
		D3D12_RESOURCE_STATES m_currentState = D3D12_RESOURCE_STATE_COMMON; //!< 追跡中の現在 Resource State
	};

	/**
	 * @brief GPU 完了まで寿命を保持する永続マップ済み Upload Buffer
	 * @thread_safety Not thread-safe. Access must be synchronized externally.
	 */
	class DX12UploadBuffer
	{
	public:

		DX12UploadBuffer() = default;
		~DX12UploadBuffer();

		GE_DISABLE_COPY_AND_MOVE(DX12UploadBuffer);

		/**
		 * @brief Upload Heap 上に CPU 書き込み用 Buffer を作成してマップする
		 * @param device Buffer を作成するデバイス
		 * @param completionFence Buffer の GPU 使用完了を追跡する Fence
		 * @param size Buffer サイズ
		 * @return 作成とマップに成功した場合は true
		 */
		bool initialize(ID3D12Device& device, const DX12Fence& completionFence, std::uint64_t size);

		/**
		 * @brief GPU 使用完了を待機して Buffer をアンマップ・解放する
		 * @return 解放に成功した場合は true
		 */
		bool finalize();

		/**
		 * @brief マップ済み領域へデータを書き込む
		 * @param data 書き込むデータ
		 * @param offset 書き込み先オフセット
		 * @return 書き込みに成功した場合は true
		 */
		bool write(std::span<const std::byte> data, std::uint64_t offset = 0);

		/**
		 * @brief Buffer が GPU で参照される提出 Fence 値を記録する
		 * @param fenceValue Buffer 使用コマンドの提出後に通知された Fence 値
		 * @return Fence 値を記録できた場合は true
		 */
		bool markUsed(std::uint64_t fenceValue);

		/**
		 * @brief Buffer の GPU Virtual Address を取得する
		 * @return GPU Virtual Address。未初期化時は 0
		 */
		[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS getGpuVirtualAddress() const noexcept;

		/**
		 * @brief Buffer のサイズを取得する
		 * @return Buffer サイズ
		 */
		[[nodiscard]] std::uint64_t getSize() const noexcept { return m_size; }

		/**
		 * @brief 内部 Resource を取得する
		 * @return 非所有の Resource 参照。未初期化時は nullptr
		 */
		[[nodiscard]] ID3D12Resource* get() const noexcept { return m_resource.get(); }

	private:

		DX12Resource m_resource; //!< Upload Heap 上の Resource
		const DX12Fence* m_completionFence = nullptr; //!< GPU 使用完了 Fence への非所有参照
		std::byte* m_mappedData = nullptr; //!< 永続マップした CPU 書き込み先
		std::uint64_t m_size = 0; //!< Buffer サイズ
		std::uint64_t m_lastUsedFenceValue = 0; //!< 解放前に完了が必要な Fence 値
	};

	/**
	 * @brief GPU 完了後に CPU から読み取る Readback Buffer
	 * @thread_safety Not thread-safe. Access must be synchronized externally.
	 */
	class DX12ReadbackBuffer
	{
	public:

		DX12ReadbackBuffer() = default;
		~DX12ReadbackBuffer();

		GE_DISABLE_COPY_AND_MOVE(DX12ReadbackBuffer);

		/**
		 * @brief Readback Heap 上に GPU Copy の受け取り先 Buffer を作成する
		 * @param device Buffer を作成するデバイス
		 * @param completionFence Copy の完了を追跡する Fence
		 * @param size Buffer サイズ
		 * @return 作成に成功した場合は true
		 */
		bool initialize(ID3D12Device& device, const DX12Fence& completionFence, std::uint64_t size);

		/**
		 * @brief GPU 完了を待機して Buffer を解放する
		 * @return 解放に成功した場合は true
		 */
		bool finalize();

		/**
		 * @brief Copy を提出した Fence 値を記録する
		 * @param fenceValue Copy コマンド提出後に通知された Fence 値
		 * @return Fence 値を記録できた場合は true
		 */
		bool markPending(std::uint64_t fenceValue);

		/**
		 * @brief GPU Copy 完了後に Buffer から CPU メモリへ読み取る
		 * @param destination 読み取り先メモリ
		 * @param offset Buffer 内の読み取り開始オフセット
		 * @return Fence 待機と読み取りに成功した場合は true
		 */
		bool read(std::span<std::byte> destination, std::uint64_t offset = 0) const;

		/**
		 * @brief Buffer のサイズを取得する
		 * @return Buffer サイズ
		 */
		[[nodiscard]] std::uint64_t getSize() const noexcept { return m_size; }

		/**
		 * @brief 内部 Resource を取得する
		 * @return 非所有の Resource 参照。未初期化時は nullptr
		 */
		[[nodiscard]] ID3D12Resource* get() const noexcept { return m_resource.get(); }

	private:

		DX12Resource m_resource; //!< Readback Heap 上の Resource
		const DX12Fence* m_completionFence = nullptr; //!< GPU Copy 完了 Fence への非所有参照
		std::uint64_t m_size = 0; //!< Buffer サイズ
		std::uint64_t m_pendingFenceValue = 0; //!< 読み取り・解放前に完了が必要な Fence 値
	};
} // namespace Engine
