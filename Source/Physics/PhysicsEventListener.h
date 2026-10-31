#pragma once

#include "Core\Math\MathTypes.h"

namespace Engine
{
    class GameObject;

    /**
     * @brief 物理演算ステップ後に通知される、エンジン側の衝突情報。
     */
    struct CollisionInfo
    {
        GameObject* other = nullptr; //!< 相手のオブジェクト。リスナーのコールバック中のみ有効。
        Vector3 point = Vector3::Zero; //!< ワールド座標系での接触点。
        Vector3 normal = Vector3::Zero; //!< 相手側から通知先へ向かう法線。
        float impulse = 0.0f; //!< 最初に報告された接触点の力積の大きさ。
    };

    /**
     * @brief 衝突およびトリガー通知をゲームプレイ側で受け取るためのインターフェース。
     * @details ComponentまたはBehaviourとともに実装する。通知はゲーム更新スレッド上で呼び出される。
     * @thread_safety メインスレッド（ゲーム更新スレッド）からのみ使用可能。
     */
    class PhysicsEventListener
    {
    public:

        virtual ~PhysicsEventListener() = default;

        /** @brief 他のオブジェクトがこのオブジェクトのトリガーに進入したときに呼び出される。 */
        virtual void onTriggerEnter(GameObject&) {}
        /** @brief 他のオブジェクトがこのオブジェクトのトリガー内に留まっているときに呼び出される。 */
        virtual void onTriggerStay(GameObject&) {}
        /** @brief 他のオブジェクトがこのオブジェクトのトリガーから退出したときに呼び出される。 */
        virtual void onTriggerExit(GameObject&) {}

        /** @brief 他のオブジェクトがこのオブジェクトとの衝突を開始したときに呼び出される。 */
        virtual void onCollisionEnter(const CollisionInfo&) {}
        /** @brief 他のオブジェクトがこのオブジェクトとの衝突を継続しているときに呼び出される。 */
        virtual void onCollisionStay(const CollisionInfo&) {}
        /** @brief 他のオブジェクトがこのオブジェクトとの衝突を終了したときに呼び出される。 */
        virtual void onCollisionExit(const CollisionInfo&) {}
    };
} // namespace Engine
