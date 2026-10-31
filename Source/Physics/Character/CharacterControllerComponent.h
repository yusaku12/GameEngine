#pragma once

#include "Core\GameObject\Component.h"
#include "Physics\PhysicsLayer.h"

namespace Engine
{
    class PhysicsWorld;

    /**
     * @brief PhysicsWorldが管理するPhysXカプセルコントローラー。
     * @thread_safety メインスレッド（ゲーム更新スレッド）からのみ使用可能。物理シミュレーションの実行前に移動すること。
     */
    class CharacterControllerComponent final : public Component
    {
    public:

        /** @brief ワールド座標系での移動量を適用し、経路上の衝突を解決する。 */
        void move(const Vector3& displacement, float deltaTime) noexcept;

        /** @brief カプセルの半径を設定する。全高は半径の2倍より大きくする必要がある。 */
        void setRadius(float radius) noexcept;
        /** @brief カプセルの半径を取得する。 */
        float getRadius() const noexcept { return m_radius; }

        /** @brief 両端の半球を含むカプセル全高を設定する。 */
        void setHeight(float height) noexcept;
        /** @brief 両端の半球を含むカプセル全高を取得する。 */
        float getHeight() const noexcept { return m_height; }

        /** @brief 直近の移動でコントローラー下方の面に接触したかどうかを取得する。 */
        bool isGrounded() const noexcept { return m_isGrounded; }
        /** @brief 足元のワールド座標を取得する。 */
        const Vector3& getPosition() const noexcept { return m_position; }

        /** @brief コントローラーが使用する衝突レイヤーを設定する。 */
        void setLayer(PhysicsLayer layer) noexcept;
        /** @brief コントローラーが使用する衝突レイヤーを取得する。 */
        PhysicsLayer getLayer() const noexcept { return m_layer; }
        /** @brief コントローラーが使用する双方向衝突マスクを設定する。 */
        void setCollisionMask(PhysicsLayerMask mask) noexcept;
        /** @brief コントローラーが使用する双方向衝突マスクを取得する。 */
        PhysicsLayerMask getCollisionMask() const noexcept { return m_collisionMask; }

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

    protected:

        void onImGui() override;
        void onPayloadRestored() override;
        void onEnable() override;
        void onDisable() override;
        void onDestroy() override;

    private:

        friend class PhysicsWorld;

        float m_radius = 0.5f;
        float m_height = 2.0f;
        PhysicsLayer m_layer = PhysicsLayer::Default;
        PhysicsLayerMask m_collisionMask = ALL_PHYSICS_LAYERS;
        Vector3 m_position = Vector3::Zero;
        bool m_isGrounded = false;
    };
} // namespace Engine
