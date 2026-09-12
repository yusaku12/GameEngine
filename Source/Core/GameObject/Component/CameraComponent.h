#pragma once

#include "Core\GameObject\Component.h"
#include "Graphics\Camera\CameraData.h"

namespace Engine
{
    /**
     * @brief GameObjectのTransformから描画Camera情報を生成するComponent。
     * @thread_safety Main thread only. Render threadにはRenderViewを渡す。
     */
    class CameraComponent final : public Component
    {
    public:

        CameraComponent() noexcept;
        ~CameraComponent() override = default;

        CameraComponent(const CameraComponent&) = delete;
        CameraComponent& operator=(const CameraComponent&) = delete;

        /**
         * @brief Projection方式を設定する。
         * @param mode 使用するProjection方式。
         */
        void setProjectionMode(CameraProjectionMode mode) noexcept;

        /**
         * @brief Projection方式を取得する。
         * @return 現在のProjection方式。
         */
        CameraProjectionMode getProjectionMode() const noexcept { return m_projectionMode; }

        /**
         * @brief 垂直FOVを度単位で設定する。
         * @param fieldOfViewDegrees 1度以上179度以下のFOV。
         */
        void setFieldOfView(float fieldOfViewDegrees) noexcept;

        /**
         * @brief 垂直FOVを取得する。
         * @return 度単位のFOV。
         */
        float getFieldOfView() const noexcept { return m_fieldOfViewDegrees; }

        /**
         * @brief Near Clip距離を設定する。
         * @param nearClip 0より大きくFar Clip未満の距離。
         */
        void setNearClipPlane(float nearClip) noexcept;

        /**
         * @brief Near Clip距離を取得する。
         * @return Near Clip距離。
         */
        float getNearClipPlane() const noexcept { return m_nearClip; }

        /**
         * @brief Far Clip距離を設定する。
         * @param farClip Near Clipより大きい距離。
         */
        void setFarClipPlane(float farClip) noexcept;

        /**
         * @brief Far Clip距離を取得する。
         * @return Far Clip距離。
         */
        float getFarClipPlane() const noexcept { return m_farClip; }

        /**
         * @brief Aspect Ratioを設定する。
         * @param aspectRatio 0より大きい横幅と高さの比。
         */
        void setAspectRatio(float aspectRatio) noexcept;

        /**
         * @brief Aspect Ratioを取得する。
         * @return 横幅と高さの比。
         */
        float getAspectRatio() const noexcept { return m_aspectRatio; }

        /**
         * @brief Orthographicの縦方向Half Sizeを設定する。
         * @param size 0より大きいHalf Size。
         */
        void setOrthographicSize(float size) noexcept;

        /**
         * @brief Orthographic Sizeを取得する。
         * @return 縦方向Half Size。
         */
        float getOrthographicSize() const noexcept { return m_orthographicSize; }

        /**
         * @brief 正規化Viewportを設定する。
         * @param viewport 0～1の範囲に収まるViewport。
         */
        void setViewport(const CameraViewport& viewport) noexcept;

        /**
         * @brief 正規化Viewportを取得する。
         * @return 現在のViewport。
         */
        const CameraViewport& getViewport() const noexcept { return m_viewport; }

        /**
         * @brief Render Target Sizeを設定しAspect Ratioを更新する。
         * @param width Pixel幅。
         * @param height Pixel高さ。0の場合は変更しない。
         */
        void setRenderTargetSize(std::uint32_t width, std::uint32_t height) noexcept;

        /**
         * @brief View行列を取得する。
         * @return CPU用の非Transpose View行列。
         */
        const Matrix& getViewMatrix() const noexcept;

        /**
         * @brief Projection行列を取得する。
         * @return 左手座標系、Depth Range 0～1のProjection行列。
         */
        const Matrix& getProjectionMatrix() const noexcept;

        /**
         * @brief View Projection行列を取得する。
         * @return View * Projection行列。
         */
        const Matrix& getViewProjectionMatrix() const noexcept;

        /**
         * @brief Inverse View行列を取得する。
         * @return CameraのWorld行列。
         */
        const Matrix& getInverseViewMatrix() const noexcept;

        /**
         * @brief Inverse Projection行列を取得する。
         * @return Projectionの逆行列。
         */
        const Matrix& getInverseProjectionMatrix() const noexcept;

        /**
         * @brief Inverse View Projection行列を取得する。
         * @return View Projectionの逆行列。
         */
        const Matrix& getInverseViewProjectionMatrix() const noexcept;

        /**
         * @brief 前回提出時のView Projection行列を取得する。
         * @return 初回は現在値、それ以降は前回値。
         */
        const Matrix& getPreviousViewProjectionMatrix() const noexcept;

        /**
         * @brief CameraのWorld座標を取得する。
         * @return Camera位置。
         */
        Vector3 getPosition() const noexcept;

        /**
         * @brief Cameraの前方向を取得する。
         * @return 左手座標系の正規化Forward。
         */
        Vector3 getForward() const noexcept;

        /**
         * @brief Cameraの右方向を取得する。
         * @return 正規化Right。
         */
        Vector3 getRight() const noexcept;

        /**
         * @brief Cameraの上方向を取得する。
         * @return 正規化Up。
         */
        Vector3 getUp() const noexcept;

        /**
         * @brief World Space Frustumを取得する。
         * @return キャッシュされたFrustum。
         */
        const Frustum& getFrustum() const noexcept;

        /**
         * @brief Screen座標をWorld座標へ変換する。
         * @param screenPoint x/yはPixel座標、zは0～1のDepth。
         * @return World座標。
         */
        Vector3 screenToWorldPoint(const Vector3& screenPoint) const noexcept;

        /**
         * @brief World座標をScreen座標へ変換する。
         * @param worldPoint World座標。
         * @return x/yはPixel座標、zは0～1のDepth。
         */
        Vector3 worldToScreenPoint(const Vector3& worldPoint) const noexcept;

        /**
         * @brief Screen座標からPicking Rayを生成する。
         * @param screenPoint Pixel座標。
         * @return World Space Ray。
         */
        Ray screenPointToRay(const Vector2& screenPoint) const noexcept;

        /**
         * @brief Projection JitterをNDC単位で設定する。
         * @param jitter NDC Offset。
         */
        void setProjectionJitter(const Vector2& jitter) noexcept;

        /**
         * @brief Projection Jitterを取得する。
         * @return NDC Offset。
         */
        Vector2 getProjectionJitter() const noexcept { return m_projectionJitter; }

        /**
         * @brief Culling Maskを設定する。
         * @param mask 描画対象LayerのBit Mask。
         */
        void setCullingMask(std::uint32_t mask) noexcept { m_cullingMask = mask; }

        /**
         * @brief Culling Maskを取得する。
         * @return 描画対象LayerのBit Mask。
         */
        std::uint32_t getCullingMask() const noexcept { return m_cullingMask; }

        /**
         * @brief Camera Priorityを設定する。
         * @param priority 描画優先度。
         */
        void setPriority(int priority) noexcept { m_priority = priority; }

        /**
         * @brief Camera Priorityを取得する。
         * @return 描画優先度。
         */
        int getPriority() const noexcept { return m_priority; }

        /**
         * @brief Clear方式を設定する。
         * @param mode Clear方式。
         */
        void setClearMode(CameraClearMode mode) noexcept { m_clearMode = mode; }

        /**
         * @brief Clear方式を取得する。
         * @return Clear方式。
         */
        CameraClearMode getClearMode() const noexcept { return m_clearMode; }

        /**
         * @brief 背景Clear Colorを設定する。
         * @param color RGBA Color。
         */
        void setBackgroundColor(const Color& color) noexcept;

        /**
         * @brief 背景Clear Colorを取得する。
         * @return RGBA Color。
         */
        const Color& getBackgroundColor() const noexcept { return m_backgroundColor; }

        /**
         * @brief Renderer用のCamera snapshotを生成する。
         * @return D3D12 Resourceを含まないRenderView。
         */
        RenderView buildRenderView() const noexcept;

    protected:

        void onAwake() override;
        void onLateUpdate(float deltaTime) override;
        void onDestroy() override;
        void onImGui() override;

    private:

        /**
         * @brief View行列を無効化する。
         */
        void invalidateProjection() noexcept;

        /**
         * @brief Projection行列を無効化する。
         */
        void synchronizeView() const noexcept;

        /**
         * @brief View Projection行列を無効化する。
         */
        void synchronizeProjection() const noexcept;

        /**
         * @brief View Projection行列を無効化する。
         */
        void synchronizeViewProjection() const noexcept;

        /**
         * @brief Frustumを無効化する。
         */
        Viewport getPixelViewport() const noexcept;

        CameraProjectionMode m_projectionMode = CameraProjectionMode::Perspective; //!< Projection方式
        float m_fieldOfViewDegrees = 60.0f;                                        //!< 垂直FOV(度)
        float m_nearClip = 0.1f;                                                   //!< Near Clip距離
        float m_farClip = 1000.0f;                                                 //!< Far Clip距離
        float m_aspectRatio = 16.0f / 9.0f;                                        //!< Aspect Ratio
        float m_orthographicSize = 5.0f;                                           //!< Orthographic縦方向Half Size
        CameraViewport m_viewport{};                                               //!< 正規化Viewport
        Vector2 m_renderTargetSize = Vector2(1.0f, 1.0f);                          //!< Render Target Size
        Vector2 m_projectionJitter = Vector2::Zero;                                //!< Projection Jitter
        std::uint32_t m_cullingMask = std::numeric_limits<std::uint32_t>::max();   //!< 描画対象LayerのBit Mask
        int m_priority = 0;                                                        //!< 描画優先度
        CameraClearMode m_clearMode = CameraClearMode::SolidColor;                 //!< Clear方式
        Color m_backgroundColor = Color(0.08f, 0.16f, 0.24f, 1.0f);                //!< 背景Clear Color

        mutable Matrix m_view = Matrix::Identity;                   //!< View行列
        mutable Matrix m_projection = Matrix::Identity;             //!< Projection行列
        mutable Matrix m_viewProjection = Matrix::Identity;         //!< View Projection行列
        mutable Matrix m_inverseView = Matrix::Identity;            //!< Inverse View行列
        mutable Matrix m_inverseProjection = Matrix::Identity;      //!< Inverse Projection行列
        mutable Matrix m_inverseViewProjection = Matrix::Identity;  //!< Inverse View Projection行列
        mutable Matrix m_previousViewProjection = Matrix::Identity; //!< 前回提出時のView Projection行列
        mutable Frustum m_frustum{};                                //!< World Space Frustum
        mutable std::uint64_t m_transformRevision = 0;              //!< Transformの更新回数
        mutable bool m_viewDirty = true;                            //!< View行列が更新されているか
        mutable bool m_projectionDirty = true;                      //!< Projection行列が更新されているか
        mutable bool m_viewProjectionDirty = true;                  //!< View Projection行列が更新されているか
        mutable bool m_frustumDirty = true;                         //!< World Space Frustumが更新されているか
        mutable bool m_hasPreviousViewProjection = false;           //!< 前回提出時のView Projection行列が有効か
    };
} // namespace Engine
