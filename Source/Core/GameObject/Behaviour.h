#pragma once

#include "Core\GameObject\Component.h"

namespace Engine
{
    /**
     * @brief Lifecycleを利用するComponentの基底クラス。
     *
     * @details 派生型はComponentRegistryへexecuteLifecycle=trueで登録してください。
     * @thread_safety Main thread only.
     */
    class Behaviour : public Component
    {
    protected:
        using Component::onAwake;
        using Component::onEnable;
        using Component::onStart;
        using Component::onUpdate;
        using Component::onFixedUpdate;
        using Component::onLateUpdate;
        using Component::onDisable;
        using Component::onDestroy;
    };
} // namespace Engine