#include "Pch.h"
#include "Core\Prefab\Prefab.h"

namespace Engine
{
    bool Prefab::capture(const GameObject& root)
    {
        m_root = captureNode(root);
        m_valid = m_root.sourceGUID.isValid();
        return m_valid;
    }

    PrefabNode Prefab::captureNode(const GameObject& object)
    {
        PrefabNode node;
        node.sourceGUID = object.getGUID();
        node.name = object.getName();
        node.active = object.isActiveSelf();
        node.tag = object.getTag();
        node.layer = object.getLayer();
        node.localTransform = *object.getTransform();
        for (const std::string_view typeName : object.getComponentTypeNames())
            node.componentTypes.emplace_back(typeName);
        node.children.reserve(object.getChildCount());
        for (std::size_t index = 0; index < object.getChildCount(); ++index)
            node.children.push_back(captureNode(*object.getChild(index)));
        return node;
    }

    GameObject* Prefab::instantiate(Scene& scene) const
    {
        return m_valid ? instantiateNode(m_root, scene, nullptr) : nullptr;
    }

    GameObject* Prefab::instantiateNode(const PrefabNode& node, Scene& scene, GameObject* parent)
    {
        GameObject* object = scene.createGameObject(node.name);
        if (object == nullptr)
            return nullptr;
        *object->getTransform() = node.localTransform;
        object->setTag(node.tag);
        object->setLayer(node.layer);
        if (parent != nullptr && !object->setParent(parent, false))
        {
            object->destroy();
            return nullptr;
        }
        object->setActive(node.active);
        for (const PrefabNode& child : node.children)
        {
            if (instantiateNode(child, scene, object) == nullptr)
            {
                object->destroy();
                return nullptr;
            }
        }
        return object;
    }
} // namespace Engine