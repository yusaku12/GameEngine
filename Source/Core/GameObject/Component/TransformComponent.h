#pragma once

#include "Core\GameObject\Component.h"
#include "Core\Math\Transform.h"

namespace Engine
{
    /**
     * @brief GameObjectのローカル変換を保持する必須Component。
     * @thread_safety Main thread only.
     */
    class TransformComponent final : public Component
    {
    public:

        /**
         * @brief ローカルTransformを取得する。
         */
        Transform& localTransform() noexcept { return m_localTransform; }
        const Transform& localTransform() const noexcept { return m_localTransform; }

    protected:

        void onImGui() override;

    private:

        Transform m_localTransform; //!< GameObjectのローカル変換
    };
} // namespace Engine