#pragma once

#include <array>

#include "Core\GameObject\Component.h"
#include "Physics\PhysicsLayer.h"
#include "Physics\PhysicsMaterial.h"
#include "Physics\PhysicsTypes.h"

namespace Engine
{
    class PhysicsWorld;

    /**
     * @brief 物理形状とトリガー設定を共通化するコンポーネント。
     * @thread_safety メインスレッド（ゲーム更新スレッド）からのみ使用可能。
     */
    class ColliderComponent : public Component
    {
    public:

        ~ColliderComponent() override = default;

        /** @brief コライダーをトリガーとして扱うか設定する。 */
        void setIsTrigger(bool enabled) noexcept;
        /** @brief コライダーがトリガーかどうかを取得する。 */
        bool isTrigger() const noexcept { return m_isTrigger; }

        /** @brief コライダー中心のローカルオフセットを設定する。 */
        void setCenter(const Vector3& center) noexcept;
        /** @brief コライダー中心のローカルオフセットを取得する。 */
        const Vector3& getCenter() const noexcept { return m_center; }

        /** @brief 物理マテリアルの値を設定する。 */
        void setMaterial(const PhysicsMaterial& material) noexcept;
        /** @brief 物理マテリアルの値を取得する。 */
        const PhysicsMaterial& getMaterial() const noexcept { return m_material; }

        /** @brief 衝突レイヤーを設定する。 */
        void setLayer(PhysicsLayer layer) noexcept;
        /** @brief 衝突レイヤーを取得する。 */
        PhysicsLayer getLayer() const noexcept { return m_layer; }

        /** @brief 衝突を許可するレイヤービットマスクを設定する。 */
        void setCollisionMask(PhysicsLayerMask mask) noexcept;
        /** @brief 衝突を許可するレイヤービットマスクを取得する。 */
        PhysicsLayerMask getCollisionMask() const noexcept { return m_collisionMask; }

        std::uint32_t getPayloadVersion() const noexcept override;
        bool serializePayload(std::vector<std::uint8_t>& payload) const override;
        bool deserializePayload(std::uint32_t version, std::span<const std::uint8_t> payload) override;

        /**
         * @brief 物理バックエンドが使用する形状ジオメトリを、現在のスケールで取得する。
         * @param scale GameObjectのワールドスケール。
         */
        PhysicsShapeGeometry geometryForPhysics(const Vector3& scale) const noexcept
        {
            return getGeometry(scale);
        }

    protected:

        void onImGui() override;
        void onPayloadRestored() override;
        void onEnable() override;
        void onDisable() override;
        void onDestroy() override;

        /** @brief コライダー形状を現在のGameObjectのスケールに合わせて返す。 */
        virtual PhysicsShapeGeometry getGeometry(const Vector3& scale) const noexcept = 0;
        virtual bool supportsTrigger() const noexcept { return true; }

        /** @brief 形状設定変更をPhysicsWorldへ反映する。 */
        void refreshGeometry() noexcept;

    private:

        friend class PhysicsWorld;

        Vector3 m_center = Vector3::Zero;
        PhysicsMaterial m_material;
        PhysicsLayer m_layer = PhysicsLayer::Default;
        PhysicsLayerMask m_collisionMask = ALL_PHYSICS_LAYERS;
        bool m_isTrigger = false;
        std::array<char, 512> m_physicsAssetPath{};
    };
} // namespace Engine
