#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Core\Prefab\Prefab.h"
#include "Core\Serialization\ComponentSerialization.h"
#include "Core\GameObject\ComponentRegistry.h"

namespace Engine
{
    bool Prefab::capture(const GameObject& root)
    {
        try
        {
            PrefabNode captured;
            if (!captureNode(root, captured) || !captured.sourceGUID.isValid())
            {
                m_valid = false;
                return false;
            }

            m_root = std::move(captured);
            m_valid = true;
            return true;
        }
        catch (const std::bad_alloc&)
        {
            m_valid = false;
            LOG_ERROR("Failed to allocate memory while capturing a Prefab.");
            return false;
        }
        catch (const std::length_error&)
        {
            m_valid = false;
            LOG_ERROR("Prefab data exceeded a collection capacity while capturing.");
            return false;
        }
    }

    bool Prefab::captureNode(const GameObject& object, PrefabNode& node)
    {
        node.sourceGUID = object.getGUID();
        node.name = object.getName();
        node.active = object.isActiveSelf();
        node.tag = object.getTag();
        node.layer = object.getLayer();
        node.localTransform = *object.getTransform();
        for (const std::string_view typeName : object.getComponentTypeNames())
            node.componentTypes.emplace_back(typeName);
        if (!Serialization::captureComponents(object, node.components))
            return false;
        node.children.reserve(object.getChildCount());
        for (std::size_t index = 0; index < object.getChildCount(); ++index)
        {
            node.children.emplace_back();
            if (!captureNode(*object.getChild(index), node.children.back()))
                return false;
        }
        return true;
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

        try
        {
            *object->getTransform() = node.localTransform;
            object->setTag(node.tag);
            object->setLayer(node.layer);
            if (parent != nullptr && !object->setParent(parent, false))
            {
                object->destroy();
                return nullptr;
            }
            object->setActive(node.active);
            if (!node.components.empty())
            {
                if (!Serialization::restoreComponents(*object, node.components))
                {
                    object->destroy();
                    return nullptr;
                }
            }
            else
            {
                for (const std::string& componentType : node.componentTypes)
                {
                    if (object->addComponent(componentType) == nullptr
                        && ComponentRegistry::instance().findByName(componentType) != nullptr)
                    {
                        LOG_ERROR("Failed to create registered component while instantiating Prefab: {}", componentType);
                        object->destroy();
                        return nullptr;
                    }
                }
            }

            for (const PrefabNode& child : node.children)
            {
                if (instantiateNode(child, scene, object) == nullptr)
                {
                    object->destroy();
                    return nullptr;
                }
            }
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while instantiating Prefab node: {}", node.name);
            object->destroy();
            return nullptr;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("Prefab node exceeded a collection capacity while instantiating: {}", node.name);
            object->destroy();
            return nullptr;
        }
        return object;
    }
} // namespace Engine