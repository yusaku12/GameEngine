#pragma once

#include "Core\CoreDefines.h"

namespace Engine
{
    class CameraComponent;

    /**
     * @brief Scene内のCamera登録とActive Cameraを管理する。
     * @thread_safety Main thread only.
     */
    class CameraManager
    {
    public:

        /**
         * @brief CameraManagerのSingletonを取得する。
         * @return CameraManagerの参照。
         */
        static CameraManager& instance() noexcept;

        CameraManager(const CameraManager&) = delete;
        CameraManager& operator=(const CameraManager&) = delete;

        /**
         * @brief Cameraを登録する。
         * @param camera 登録する非所有Cameraポインタ。
         */
        void registerCamera(CameraComponent* camera);

        /**
         * @brief Cameraの登録を解除する。
         * @param camera 登録解除するCameraポインタ。
         */
        void unregisterCamera(CameraComponent* camera) noexcept;

        /**
         * @brief Main Cameraを設定する。
         * @param camera 登録済みCamera、またはnullptr。
         * @return 設定できた場合はtrue。
         */
        bool setMainCamera(CameraComponent* camera) noexcept;

        /**
         * @brief Main Cameraを取得する。
         * @return Main Camera。未設定の場合はnullptr。
         */
        CameraComponent* getMainCamera() const noexcept { return m_mainCamera; }

        /**
         * @brief Editor Cameraを設定する。
         * @param camera 登録済みCamera、またはnullptr。
         * @return 設定できた場合はtrue。
         */
        bool setEditorCamera(CameraComponent* camera) noexcept;

        /**
         * @brief Editor Cameraを取得する。
         * @return Editor Camera。未設定の場合はnullptr。
         */
        CameraComponent* getEditorCamera() const noexcept { return m_editorCamera; }

        /**
         * @brief Editor Cameraの使用状態を設定する。
         * @param enabled Editor CameraをActiveにする場合はtrue。
         */
        void setEditorCameraActive(bool enabled) noexcept { m_editorCameraActive = enabled; }

        /**
         * @brief 現在描画に使用するCameraを取得する。
         * @return 有効なActive Camera。存在しない場合はnullptr。
         */
        CameraComponent* getActiveCamera() const noexcept;

        /**
         * @brief 登録済みCameraを取得する。
         * @return 非所有Cameraポインタの配列。
         */
        const std::vector<CameraComponent*>& getCameras() const noexcept { return m_cameras; }

        /**
         * @brief Render Target Sizeを全Cameraへ反映する。
         * @param width Pixel幅。
         * @param height Pixel高さ。
         */
        void setRenderTargetSize(std::uint32_t width, std::uint32_t height) noexcept;

        /**
         * @brief 全Camera登録と選択状態を破棄する。
         */
        void shutdown() noexcept;

    private:
        CameraManager() = default;
        ~CameraManager() = default;

        /**
         * @brief Cameraが登録済みか判定する。
         * @param camera 判定するCameraポインタ。
         * @return 登録済みの場合はtrue。
         */
        bool contains(const CameraComponent* camera) const noexcept;

        std::vector<CameraComponent*> m_cameras;   //!< 登録済みCameraの非所有ポインタ配列
        CameraComponent* m_mainCamera = nullptr;   //!< Main Cameraの非所有ポインタ
        CameraComponent* m_editorCamera = nullptr; //!< Editor Cameraの非所有ポインタ
        std::uint32_t m_renderTargetWidth = 1;     //!< Render Targetの幅
        std::uint32_t m_renderTargetHeight = 1;    //!< Render Targetの高さ
        bool m_editorCameraActive = false;         //!< Editor Cameraの使用状態
    };
} // namespace Engine
