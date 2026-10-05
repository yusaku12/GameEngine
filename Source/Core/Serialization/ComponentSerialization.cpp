#include "Pch.h"
#include "Core\Logging\Logging.h"
#include "Core\Serialization\ComponentSerialization.h"
#include "Core\GameObject\Component\TransformComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"

namespace Engine::Serialization
{
    bool captureComponents(const GameObject& object, std::vector<ComponentSnapshot>& snapshots)
    {
        snapshots.clear();
        try
        {
            std::vector<ComponentSnapshot> captured;
            bool valid = true;
            object.forEachComponent([&](const Component& component, const std::type_index type)
                {
                    if (!valid || type == std::type_index(typeid(TransformComponent)))
                        return;
                    const ComponentTypeInfo* info = ComponentRegistry::instance().get(type);
                    if (info == nullptr)
                        return;
                    ComponentSnapshot snapshot;
                    snapshot.type = info->name;
                    snapshot.enabled = component.isEnabled();
                    snapshot.payloadVersion = component.getPayloadVersion();
                    valid = component.serializePayload(snapshot.payload);
                    if (valid)
                        captured.push_back(std::move(snapshot));
                });
            if (!valid)
            {
                LOG_ERROR("Failed to serialize a Component while capturing its snapshot.");
                return false;
            }
            std::stable_sort(captured.begin(), captured.end(),
                [](const ComponentSnapshot& left, const ComponentSnapshot& right)
                {
                    return left.type < right.type;
                });
            snapshots.swap(captured);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            LOG_ERROR("Failed to allocate memory while capturing Component snapshots.");
            return false;
        }
        catch (const std::length_error&)
        {
            LOG_ERROR("Component snapshots exceeded a collection capacity.");
            return false;
        }
    }

    bool restoreComponents(GameObject& object, const std::vector<ComponentSnapshot>& snapshots)
    {
        const ComponentRegistry& registry = ComponentRegistry::instance();
        for (std::size_t index = 0; index < snapshots.size(); ++index)
        {
            const ComponentSnapshot& snapshot = snapshots[index];
            const ComponentTypeInfo* info = registry.findByName(snapshot.type);
            if (info == nullptr)
                continue;
            if (!info->allowMultiple)
            {
                for (std::size_t previous = 0; previous < index; ++previous)
                {
                    if (snapshots[previous].type == snapshot.type)
                    {
                        LOG_ERROR("Serialized data contains duplicate non-multiple component: {}", snapshot.type);
                        return false;
                    }
                }
            }

            Component* component = object.addComponent(snapshot.type);
            if (component == nullptr)
            {
                LOG_ERROR("Failed to create registered component while restoring: {}", snapshot.type);
                return false;
            }
            component->setEnabled(snapshot.enabled);
            if (!component->deserializePayload(snapshot.payloadVersion, snapshot.payload))
            {
                LOG_ERROR("Failed to deserialize component payload: {}", snapshot.type);
                return false;
            }
        }
        return true;
    }
}