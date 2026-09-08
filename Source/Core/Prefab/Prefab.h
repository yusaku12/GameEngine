#pragma once

#include <string>
#include <vector>

#include "Core\GameObject\GameObject.h"
#include "Core\Scene\Scene.h"

namespace Engine
{
    /**
     * @brief Prefab内のGameObjectスナップショット。
     */
    struct PrefabNode
    {
        ObjectGUID sourceGUID;
        std::string name;
        bool active = true;
        TagID tag = 0;
        LayerID layer = 0;
        Transform localTransform;
        std::vector<std::string> componentTypes;
        std::vector<PrefabNode> children;
    };

    /**
     * @brief GameObject階層を再利用するPrefabアセット。
     * @thread_safety Main thread only.
     */
    class Prefab
    {
    public:
        Prefab() = default;

        [[nodiscard]] bool capture(const GameObject& root);
        [[nodiscard]] GameObject* instantiate(Scene& scene) const;

        [[nodiscard]] const PrefabNode& getRoot() const noexcept { return m_root; }
        [[nodiscard]] bool isValid() const noexcept { return m_valid; }

    private:
        static PrefabNode captureNode(const GameObject& object);
        static GameObject* instantiateNode(const PrefabNode& node, Scene& scene, GameObject* parent);

        PrefabNode m_root;
        bool m_valid = false;
    };
} // namespace Engine