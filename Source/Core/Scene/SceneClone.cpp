#include "Pch.h"
#include "Core\Scene\SceneClone.h"
#include "Core\Scene\Scene.h"
#include "Core\Serialization\ComponentSerialization.h"

namespace Engine
{
    bool cloneScenes(const std::span<const SceneClonePair> scenes)
    {
        for (std::size_t index = 0; index < scenes.size(); ++index)
        {
            const auto& pair = scenes[index];
            if (pair.destination.getGameObjectCount() != 0 || pair.source.isBusy()
                || pair.destination.isBusy() || &pair.source == &pair.destination)
            {
                LOG_ERROR("[Play] Scene clone requires idle sources and empty destinations.");
                return false;
            }
            for (const auto& other : scenes)
            {
                if (&pair.destination == &other.source)
                {
                    LOG_ERROR("[Play] Clone destinations cannot also be sources.");
                    return false;
                }
            }
            for (std::size_t previous = 0; previous < index; ++previous)
            {
                if (&pair.destination == &scenes[previous].destination)
                {
                    LOG_ERROR("[Play] Duplicate scene clone destination.");
                    return false;
                }
            }
        }
        const auto rollback = [&scenes]
        {
            for (const auto& pair : scenes)
                pair.destination.clear();
        };
        try
        {
            ObjectReferenceMap objects;
            for (const auto& pair : scenes)
            {
                for (const auto& source : pair.source.getGameObjects())
                {
                    GameObject* target = pair.destination.createGameObject(source->getName(), source->getGUID());
                    if (target == nullptr || !objects.emplace(source->getGUID(), target).second)
                    {
                        LOG_ERROR("[Play] Could not clone object; GUIDs must be unique across scenes.");
                        rollback();
                        return false;
                    }
                    target->setActive(source->isActiveSelf());
                    target->setTag(source->getTag());
                    target->setLayer(source->getLayer());
                    *target->getTransform() = *source->getTransform();
                    target->getComponent<TransformComponent>()->setEnabled(
                        source->getComponent<TransformComponent>()->isEnabled());
                }
            }
            for (const auto& pair : scenes)
            {
                for (const auto& source : pair.source.getGameObjects())
                {
                    GameObject& target = *objects.at(source->getGUID());
                    if (source->getParent() != nullptr
                        && !target.setParent(objects.at(source->getParent()->getGUID()), false))
                    {
                        LOG_ERROR("[Play] Could not clone object hierarchy.");
                        rollback();
                        return false;
                    }
                    bool copied = true;
                    source->forEachComponent([&](const Component& component, const std::type_index& type)
                    {
                        if (!copied || type == std::type_index(typeid(TransformComponent)))
                            return;
                        const ComponentTypeInfo* info = ComponentRegistry::instance().get(type);
                        Component* destination = info != nullptr ? target.addComponent(info->name) : nullptr;
                        copied = destination != nullptr && component.copyConfigurationTo(*destination);
                        if (copied)
                            destination->setEnabled(component.isEnabled());
                        else
                            LOG_ERROR("[Play] Could not clone component: {}", info != nullptr ? info->name : "unregistered");
                    });
                    if (!copied)
                    {
                        rollback();
                        return false;
                    }
                }
            }
            for (const auto& [guid, object] : objects)
            {
                GE_UNUSED(guid);
                if (!Serialization::resolveObjectReferences(*object, objects))
                {
                    rollback();
                    return false;
                }
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("[Play] Insufficient memory to clone scenes.");
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("[Play] Scene clone exceeded collection capacity.");
        }
        catch (const std::out_of_range&)
        {
            LOG_ERROR("[Play] Scene hierarchy references an object outside the cloned scenes.");
        }
        rollback();
        return false;
    }
}
