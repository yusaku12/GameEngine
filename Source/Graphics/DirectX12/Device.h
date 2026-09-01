#pragma once

#include <d3d12.h>
#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Core\CoreDefines.h"

namespace Engine
{
	/**
	 * @brief DirectX 12 デバイスの初期化設定
	 */
	struct DX12DeviceConfig
	{
		bool enableDebugLayer = false; //!< D3D12 デバッグレイヤーを有効にするか
		bool enableGpuBasedValidation = false; //!< GPU-based validation を有効にするか
		bool enableDxgiDebug = false; //!< DXGI Debug の Live Object Report を有効にするか
	};

	/**
	 * @brief DirectX 12 デバイスが持つ基本機能
	 */
	struct DX12DeviceCapabilities
	{
		D3D12_RESOURCE_BINDING_TIER resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1; //!< Resource Binding Tier
	};

	/**
	 * @brief DXGI Factory、アダプター、および DirectX 12 デバイスを管理するクラス
	 * @thread_safety Not thread-safe. Access must be synchronized externally.
	 */
	class DX12Device
	{
	public:

		DX12Device() = default;
		~DX12Device() = default;

		GE_DISABLE_COPY_AND_MOVE(DX12Device);

		/**
		 * @brief ハードウェアアダプターを優先して DirectX 12 デバイスを作成する
		 * @param config 初期化設定
		 * @return 作成に成功した場合は true
		 */
		bool initialize(const DX12DeviceConfig& config = DX12DeviceConfig{});

		/**
		 * @brief 保持している DirectX 12 オブジェクトを解放する
		 */
		void finalize();

		/**
		 * @brief 初期化済みかを取得する
		 * @return 初期化済みの場合は true
		 */
		[[nodiscard]] bool isInitialized() const noexcept { return m_device != nullptr; }

		/**
		 * @brief DirectX 12 デバイスを取得する
		 * @return 非所有のデバイス参照。未初期化時は nullptr
		 */
		[[nodiscard]] ID3D12Device* get() const noexcept { return m_device.Get(); }

		/**
		 * @brief DXGI Factory を取得する
		 * @return 非所有の Factory 参照。未初期化時は nullptr
		 */
		[[nodiscard]] IDXGIFactory6* getFactory() const noexcept { return m_factory.Get(); }

		/**
		 * @brief 選択したアダプターを取得する
		 * @return 非所有のアダプター参照。未初期化時は nullptr
		 */
		[[nodiscard]] IDXGIAdapter4* getAdapter() const noexcept { return m_adapter.Get(); }

		/**
		 * @brief 取得済みのデバイス機能を取得する
		 * @return デバイス機能
		 */
		[[nodiscard]] const DX12DeviceCapabilities& getCapabilities() const noexcept { return m_capabilities; }

		/**
		 * @brief Device Removed Reason をログへ出力する
		 * @return Device Removed が検出された場合は true
		 */
		bool logDeviceRemovedReason() const;

	private:

		bool enableDebugLayer(bool enableGpuBasedValidation);
		void enableDxgiDebug();
		bool selectAdapter();
		bool createDevice();
		bool queryCapabilities();

		Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory; //!< DXGI Factory
		Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter; //!< 選択した GPU アダプター
		Microsoft::WRL::ComPtr<ID3D12Device> m_device; //!< DirectX 12 デバイス
		Microsoft::WRL::ComPtr<IDXGIDebug1> m_dxgiDebug; //!< DXGI Debug Interface
		DX12DeviceCapabilities m_capabilities; //!< デバイス機能
	};
} // namespace Engine
